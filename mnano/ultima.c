/*
  ultima.c - the core switch of Tang Ultima.  See ultima.h.

  The mechanism, in one paragraph: the Tang Nano 20K's 8 MB flash holds
  the three bitstreams at 1 MB slots (../Makefile: UKNC at 0, PK8000 at
  0x100000, Korvet at 0x200000), each built with the next slot's address
  in its header.  The FPGA loads slot 0 at power-up.  SYS command 9
  (sys_reconfig) makes the running core pulse RECONFIG_N, on which the
  FPGA loads the image at the address its current header names - the
  next core in the ring.  So from any core the wanted one is one or two
  hops away, and this file walks them: it cannot pick a slot, it can
  only say "next" and look at who answers.

  Two things about the MCU's side of that.  While the FPGA reloads (a
  few seconds at Gowin's default loading rate) the SPI link is dead and
  a read of it is floating pins; the interrupt processing (spi.c) is
  held off through sys_irq_hold so that a floating "sector request" is
  not served against a mounted image.  And a core that has just loaded
  raises its coldboot notice, which the firmware otherwise answers by
  resetting the MCU (sysctrl.c) - expected here, acknowledged here.
*/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ff.h>

#include "ultima.h"
#include "sysctrl.h"
#include "sdc.h"

#ifndef SDL
#include <FreeRTOS.h>
#include <task.h>
#endif

// The ring, in flash order.  The Makefile's CORES and this table say
// the same thing twice; the order is what makes the hop count right.
const ultima_core_t ultima_cores[ULTIMA_CORES] = {
  { CORE_ID_UKNC,   "UKNC",   "uknc",   "uknc.ini"   },
  { CORE_ID_PK8000, "PK8000", "pk8000", "pk8000.ini" },
  { CORE_ID_KORVET, "Korvet", "korvet", "korvet.ini" },
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

// the card's wish: "core=korvet" (the directory name or the OSD's name,
// case does not matter) in /sd/ultima.ini; anything else in the file is
// ignored, so it can carry comments
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
    f_puts("; Tang Ultima - the core to run; the OSD's Core form writes this\n", &fil);
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

#ifndef SDL

// One hop: tell the core to reconfigure, then wait for whoever comes up.
// The old core is gone within microseconds of the pulse, but its last
// answers may still be in flight, so nothing is believed for the first
// while; then a core has ten seconds to answer, three of which the load
// takes at the default rate.  Returns 0 with core_id set to the new
// core, -1 when nothing answered.
static int ultima_hop(spi_t *spi) {
  sys_reconfig(spi);
  vTaskDelay(pdMS_TO_TICKS(300));
  for(int t=0;t<1000;t++) {
    if(sys_status_is_valid(spi)) {
      sys_set_val(spi, 'R', 3);   // hold the machine, as main() does
      return 0;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return -1;
}

// walk the ring to `want`; -1 if it could not be reached
static int ultima_walk(spi_t *spi, unsigned char want) {
  for(int hops=0; hops<ULTIMA_CORES && core_id != want; hops++) {
    unsigned char was = core_id;
    const ultima_core_t *c = ultima_core(want);
    printf("core %02x running, %s (%02x) wanted: reconfiguring\r\n", was, c->name, want);
    if(ultima_hop(spi) < 0) {
      printf("no core answered after the reconfiguration\r\n");
      return -1;
    }
    if(core_id == was) {
      // the same core again: a bitstream without CMD 9, or a flash with
      // one image whose header names itself
      printf("the core did not change - no MultiBoot image to go to\r\n");
      return -1;
    }
  }
  return core_id == want ? 0 : -1;
}

void ultima_boot(spi_t *spi) {
  unsigned char want = ultima_wanted();
  const ultima_core_t *running = ultima_core(core_id);

  printf("Tang Ultima: running %s (%02x), card asks for %02x\r\n",
	 running ? running->name : "an unknown core", core_id, want);

  if(want && want != core_id && ultima_core(want)) {
    sys_irq_hold = 1;
    int r = ultima_walk(spi, want);
    if(sys_status_is_valid(spi)) {
      // the new core's coldboot notice is ours, not a reason to reset;
      // its sd_card.v has brought the card up again, so mount afresh
      sys_irq_ctrl(spi, 0x01);
      sdc_reattach();
    }
    sys_irq_hold = 0;
    if(r < 0) printf("Tang Ultima: staying on core %02x\r\n", core_id);
  }
  ultima_mkdir();
}

void ultima_switch(spi_t *spi, unsigned char id) {
  const ultima_core_t *c = ultima_core(id);
  if(!c || id == core_id) return;

  printf("Tang Ultima: switching to %s\r\n", c->name);
  ultima_set_wanted(c);

  // Nothing may be asked of the images while the link is dead: close
  // them (the core is told they are gone, which it will not remember).
  for(int d=0;d<MAX_DRIVES;d++) sdc_image_open(d, NULL);

  // one hop now; the MCU restarts around it and ultima_boot() walks the
  // rest of the way with the card's wish in hand - one code path for
  // both the OSD's switch and the power-up
  sys_irq_hold = 1;
  sys_set_val(spi, 'R', 3);
  sys_reconfig(spi);
  vTaskDelay(pdMS_TO_TICKS(100));
  sys_reset_mcu();
  while(1) vTaskDelay(pdMS_TO_TICKS(100));
}

#else  // the host test (menu_test.c): the switch is recorded, not made

unsigned char ultima_test_switched = 0;

void ultima_boot(spi_t *spi) { (void)spi; }

void ultima_switch(spi_t *spi, unsigned char id) {
  (void)spi;
  if(ultima_core(id) && id != core_id) {
    ultima_set_wanted(ultima_core(id));
    ultima_test_switched = id;
  }
}

#endif
