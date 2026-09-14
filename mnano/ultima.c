/*
  ultima.c - the core switch of Tang Ultima.  See ultima.h.

  The mechanism, in one paragraph: the flash holds one bitstream, at
  address 0, and that is the machine the board is.  The four machines sit
  on the card as packed bitstreams, /sd/cores/<name>.bin.  Switching means
  writing the wanted one to address 0 - flashwr.c, through flashwr.v and
  the MSPI pins the FPGA hands to user logic after configuration - and
  then power-cycling the board, because power-up always loads address 0.

  This is the second design.  The first was Gowin MultiBoot: three slots,
  every bitstream's header naming the next, and SYS command 9 pulsing
  RECONFIG_N to make the FPGA jump.  On this board the pulse is provably
  generated and the FPGA provably ignores it - reusing the pad as a GPIO
  cuts it from the configuration controller, and pin 9 goes nowhere but a
  test pad, so nothing external is holding it up.  The whole account is in
  .claude/docs/progress.md.  SYS command 9 is still in every core and in
  sysctrl.c; one wire from pin 48 to that pad would bring the instant
  switch back, and nothing here would need to change but this file.

  What is gone with MultiBoot is the ring, the walk, and the need to hold
  the SPI link across a reconfiguration: the link never dies now, because
  the FPGA never reloads while the firmware is running.  What is new is
  the risk at the other end - the install erases the only bitstream the
  board can boot, so it checks the file is one before it starts, verifies
  what it wrote, and says loudly that a power cut in the middle means
  openFPGALoader.
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ff.h>

#include "ultima.h"
#include "flashwr.h"
#include "coreload.h"
#include "sysctrl.h"
#include "sdc.h"

#ifndef SDL
#include <FreeRTOS.h>
#include <task.h>
#endif

// The four.  The Makefile's CORES says the same thing; the order is only
// cosmetic now that there is no ring to walk.
const ultima_core_t ultima_cores[ULTIMA_CORES] = {
  { CORE_ID_UKNC,   "UKNC",   "uknc",   "uknc.ini"   },
  { CORE_ID_PK8000, "PK8000", "pk8000", "pk8000.ini" },
  { CORE_ID_KORVET, "Korvet", "korvet", "korvet.ini" },
  { CORE_ID_ZS256,  "ZS-256", "zs256",  "zs256.ini"  },
};

const ultima_core_t *ultima_core(unsigned char id) {
  for(int i=0;i<ULTIMA_CORES;i++)
    if(ultima_cores[i].id == id) return &ultima_cores[i];
  return NULL;
}

const char *ultima_root(void) {
  static char root[40];
  const ultima_core_t *c = ultima_core(core_id);
  if(!c) return CARD_MOUNTPOINT;
  snprintf(root, sizeof(root), CARD_MOUNTPOINT "/%s", c->dir);
  return root;
}

const char *ultima_core_path(const ultima_core_t *c) {
  static char path[48];
  snprintf(path, sizeof(path), ULTIMA_COREDIR "/%s.bin", c->dir);
  return path;
}

// what the card says is installed: "core=korvet" (the directory name or
// the OSD's name, case does not matter) in /sd/ultima.ini; anything else
// in the file is ignored, so it can carry comments
unsigned char ultima_wanted(void) {
  unsigned char id = 0;
  FIL fil;
  char line[64];

  sdc_lock();
  if(f_open(&fil, ULTIMA_INI, FA_OPEN_EXISTING | FA_READ) == FR_OK) {
    while(f_gets(line, sizeof(line), &fil)) {
      char *p = line, *e;
      while(*p == ' ' || *p == '\t') p++;
      if(strncasecmp(p, "core", 4)) continue;
      p += 4;
      while(*p == ' ' || *p == '\t') p++;
      if(*p++ != '=') continue;
      while(*p == ' ' || *p == '\t') p++;
      for(e = p; *e && *e != ';' && *e != ' ' && *e != '\r' && *e != '\n' && *e != '\t'; e++);
      *e = 0;
      for(int i=0;i<ULTIMA_CORES;i++)
        if(!strcasecmp(p, ultima_cores[i].dir) || !strcasecmp(p, ultima_cores[i].name))
          id = ultima_cores[i].id;
    }
    f_close(&fil);
  }
  sdc_unlock();
  return id;
}

static void ultima_set_wanted(const ultima_core_t *c) {
  FIL fil;
  sdc_lock();
  if(f_open(&fil, ULTIMA_INI, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
    f_puts("; Tang Ultima - the core saved in the flash, written by the\n", &fil);
    f_puts("; OSD's Core form.  Power-up loads whatever is at flash\n", &fil);
    f_puts("; address 0; this only records which one that is.\n", &fil);
    f_puts("core=", &fil);
    f_puts(c->dir, &fil);
    f_puts("\n", &fil);
    f_close(&fil);
  } else
    printf("cannot write " ULTIMA_INI "\r\n");
  sdc_unlock();
}

// the running core's directory, so a fresh card gets its folders
static void ultima_mkdir(void) {
  const ultima_core_t *c = ultima_core(core_id);
  if(!c) return;
  sdc_lock();
  FRESULT r = f_mkdir(ultima_root());
  if(r == FR_OK) printf("created %s\r\n", ultima_root());
  else if(r != FR_EXIST) printf("mkdir %s: %d\r\n", ultima_root(), r);
  sdc_unlock();
}

// Here rather than in flashwr.c so that the host menu test, which links
// ultima.c but has no SPI link to give flashwr.c, still has the strings.
const char *flash_strerror(int err) {
  if(err <= -20) return cl_strerror(err);
  switch(err) {
  case 0:                return "ok";
  case FLASH_ERR_BUS:    return "no flash on the MSPI pins";
  case FLASH_ERR_OPEN:   return "cannot open the core file";
  case FLASH_ERR_SIZE:   return "the core file is the wrong size";
  case FLASH_ERR_NOTBIT: return "not a bitstream for this FPGA";
  case FLASH_ERR_READ:   return "cannot read the core file";
  case FLASH_ERR_ERASE:  return "erase failed";
  case FLASH_ERR_WRITE:  return "write failed";
  case FLASH_ERR_VERIFY: return "what was written does not read back";
  default:               return "unknown error";
  }
}

const char *ultima_strerror(int err) { return flash_strerror(err); }

void ultima_boot(spi_t *spi) {
  unsigned char want = ultima_wanted();
  const ultima_core_t *running = ultima_core(core_id);
#ifdef SDL
  (void)spi;
#endif

  printf("Tang Ultima: running %s (%02x)\r\n",
	 running ? running->name : "an unknown core", core_id);

#ifndef SDL
  // Read-only, and it is what says whether a switch is possible at all on
  // this board: if the flash does not answer here, the Core form cannot
  // work and this is where that becomes visible - before anything is
  // erased rather than after.
  flash_probe(spi);
#endif

  // The card records what "Save to flash" last put at address 0; the
  // running machine may be another, loaded into the SRAM by a switch,
  // and that is normal now.
  if(want) {
    const ultima_core_t *c = ultima_core(want);
    printf("Tang Ultima: the flash holds %s\r\n", c ? c->name : "?");
  }
  ultima_mkdir();
}

#ifndef SDL

int ultima_switch(spi_t *spi, unsigned char id, flash_progress_t cb) {
  const ultima_core_t *c = ultima_core(id);
  if(!c) return CL_ERR_OPEN;
  if(id == core_id) return 0;

  printf("Tang Ultima: switching to %s from %s\r\n", c->name, ultima_core_path(c));

  // Nothing must be asked of the images from here on: the FPGA is about
  // to be replaced, and a sector request to a machine that is being
  // erased is a transaction that never completes.
  for(int d=0;d<MAX_DRIVES;d++) sdc_image_open(d, NULL);

  int r = cl_ping(spi);
  if(r) {
    printf("Tang Ultima: %s\r\n", cl_strerror(r));
    cl_release(spi);
    return r;
  }
  r = cl_send(spi, ultima_core_path(c), c->dir, cb);
  if(r) {
    printf("Tang Ultima: %s not sent: %s\r\n", c->name, cl_strerror(r));
    cl_release(spi);
    return r;
  }

  // From here the link is not to be trusted: the FPGA goes blank a moment
  // after stage 2 says yes, and what it shows on the interrupt line and
  // MISO until the next machine is up is not a request.  So the interrupt
  // task is held first, and the answer to the load is the last thing
  // read.  No answer at all is taken as "it happened", since a lost byte
  // is likelier than a stage 2 that stopped in the middle of saying so.
  if(cb) cb(CL_STAGE_LOAD, 0, 0);
  sys_irq_hold = 1;
  r = cl_load(spi);
  if(r == CL_ERR_REFUSED) {
    sys_irq_hold = 0;
    cl_release(spi);
    printf("Tang Ultima: %s not loaded: %s\r\n", c->name, cl_strerror(r));
    return r;
  }
  printf("Tang Ultima: %s is loading - restarting the MCU\r\n", c->name);
  vTaskDelay(pdMS_TO_TICKS(300));
  sys_reset_mcu();
  for(;;) vTaskDelay(pdMS_TO_TICKS(100));
}

int ultima_install(spi_t *spi, flash_progress_t cb) {
  const ultima_core_t *c = ultima_core(core_id);
  if(!c) return FLASH_ERR_OPEN;

  printf("Tang Ultima: saving %s to the flash from %s\r\n", c->name, ultima_core_path(c));

  // Nothing must be asked of the images while the flash is being written:
  // the card and the flash share the m0s link, and a sector request in
  // the middle of a page program is a transaction that never completes.
  for(int d=0;d<MAX_DRIVES;d++) sdc_image_open(d, NULL);

  int r = flash_install(spi, ultima_core_path(c), cb);
  if(r == 0) {
    ultima_set_wanted(c);
    printf("Tang Ultima: %s is in the flash - it is what power-up loads\r\n", c->name);
  } else
    printf("Tang Ultima: %s not saved: %s\r\n", c->name, flash_strerror(r));
  return r;
}

#else  // the host test (menu_test.c): the switch is recorded, not made

unsigned char ultima_test_switched = 0;
unsigned char ultima_test_installed = 0;

int ultima_switch(spi_t *spi, unsigned char id, flash_progress_t cb) {
  (void)spi; (void)cb;
  if(ultima_core(id) && id != core_id)
    ultima_test_switched = id;
  return 0;
}

int ultima_install(spi_t *spi, flash_progress_t cb) {
  (void)spi; (void)cb;
  if(ultima_core(core_id)) {
    ultima_set_wanted(ultima_core(core_id));
    ultima_test_installed = core_id;
  }
  return 0;
}

const char *cl_strerror(int err) { (void)err; return "coreload"; }
void cl_release(spi_t *spi) { (void)spi; }

#endif
