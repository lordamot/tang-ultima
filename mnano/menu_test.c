/*
  menu_test.c - the OSD menu on the host (make menu-test).

  menu.c is compiled with -DSDL, which is its host switch: no FreeRTOS,
  menu_init() takes a u8g2 to draw into.  Everything else it calls is
  stubbed here - the card has three files a slot, the core takes the
  values into a log, ultima.c records a switch instead of making one -
  and the menu is driven through menu_do() with the events usb_host.c
  would send.  Each screen worth looking at is dumped as a 128x64 text
  bitmap under the directory given as the first argument (tools/osd_png.py
  makes PNGs of them).

  Four cores, one firmware, so the walk is generic and runs once per
  core: the main form is read, every 'S' form is entered and left by its
  title (and checked to come back to the entry that opened it), every
  'L' value is stepped right and left and lands where it started with
  the core told each time, every 'F' selector is opened on its slot with
  its extensions and closed, every 'T' page is opened and closed, and the
  buttons are left alone but for Reset.  Then the Core form: it is on
  every core's main form, marks the running core, selecting the running
  core does nothing, selecting another records the switch.  The
  per-core detail (the UKNC's RTC form, the Korvet's Debug page, the
  PK8000's tape, the ZS-256's ROM) is in the sibling repositories' own
  tests; what this one is for is that the four sets of forms still agree with their
  cores after being put into one file, and that the Core form is right.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "u8g2.h"
#include "ff.h"
#include "diskio.h"
#include "sysctrl.h"
#include "sdc.h"
#include "extrom.h"
#include "rt11sav.h"
#include "bas.h"
#include "romload.h"
#include "ultima.h"
#include "menu.h"

unsigned char core_id = CORE_ID_UKNC;
extern unsigned char ultima_test_switched;   // ultima.c, the host half
extern unsigned char ultima_test_installed;

//------------------------------------------------------------------------
// stubs
//------------------------------------------------------------------------
void vTaskDelay(int ms) { (void)ms; }
static int extrom_inits;
void extrom_init(spi_t *spi) { (void)spi; extrom_inits++; }
static int bas_runs;
int bas_run(spi_t *spi, const char *path) { (void)spi; (void)path; bas_runs++; return 0; }
static int rom_boots, rom_selects;
static char rom_selected[256];
unsigned long rom_size_zs = 65536, rom_size_gs = 32768;
void rom_boot(spi_t *spi) { (void)spi; rom_boots++; }
void rom_select(spi_t *spi, const char *path) { (void)spi; rom_selects++; snprintf(rom_selected, sizeof(rom_selected), "%s", path); }
int rt11sav_make(const char *dir, const char *name, unsigned date, char *err, int errlen) {
  (void)dir; (void)name; (void)date; snprintf(err, errlen, "no card"); return -1;
}
unsigned rt11sav_date(int y, int m, int d) { (void)y; (void)m; (void)d; return 0; }
void sdc_lock(void) {}
void sdc_unlock(void) {}
int  sdc_is_ready(void) { return 1; }

// the OSD's visibility, as osd_enable() would set it
static int osd_visible;
void osd_enable(osd_t *osd, char en) { (void)osd; osd_visible = en; }

// FatFs never gets a volume here: f_open fails and the menu takes its
// defaults, which is the path a card without an .ini takes
DSTATUS disk_initialize(BYTE p) { (void)p; return STA_NOINIT; }
DSTATUS disk_status(BYTE p) { (void)p; return STA_NOINIT; }
DRESULT disk_read(BYTE p, BYTE *b, LBA_t s, UINT c) { (void)p; (void)b; (void)s; (void)c; return RES_NOTRDY; }
DRESULT disk_write(BYTE p, const BYTE *b, LBA_t s, UINT c) { (void)p; (void)b; (void)s; (void)c; return RES_NOTRDY; }
DRESULT disk_ioctl(BYTE p, BYTE c, void *b) { (void)p; (void)c; (void)b; return RES_NOTRDY; }

// what the core was told: a log of (id, value)
static char set_log[512][2];
static int  set_n;
void sys_set_val(spi_t *spi, char id, uint8_t v) {
  (void)spi;
  if(set_n < 512) { set_log[set_n][0] = id; set_log[set_n][1] = v; }
  set_n++;
}
void sys_get_debug(spi_t *spi, unsigned char *buf, int len) { (void)spi; memset(buf, 0, len); }
void sys_get_rtc(spi_t *spi, sys_rtc_t *t) { (void)spi; memset(t, 0, sizeof(*t)); t->year = 2026; t->month = 1; t->date = 1; t->dow = 5; }
static int set_last(char id) {
  for(int i=set_n-1;i>=0;i--) if(set_log[i][0] == id) return set_log[i][1];
  return -1;
}
static int set_count(char id) {
  int n = 0;
  for(int i=0;i<set_n;i++) if(set_log[i][0] == id) n++;
  return n;
}

// the card: one directory a slot, three files each
static char *cwd[MAX_DRIVES + 1];
static char *image_name[MAX_DRIVES + 1];
static int  open_drive = -1, open_n;          // the last sdc_image_open()
static int  readdir_drive = -1;               // the last sdc_readdir()
static const char *readdir_exts;
char *sdc_get_image_name(int drive) { return image_name[drive]; }
char *sdc_get_cwd(int drive) { if(!cwd[drive]) cwd[drive] = strdup(ultima_root()); return cwd[drive]; }
void sdc_set_default(int drive, const char *name) { (void)drive; (void)name; }
void sdc_set_image_name(int drive, const char *name) {
  if(image_name[drive]) free(image_name[drive]);
  image_name[drive] = name ? strdup(name) : NULL;
}
int sdc_image_open(int drive, char *name) {
  assert(drive >= 0 && drive < MAX_DRIVES);
  sdc_set_image_name(drive, name);
  open_drive = drive; open_n++;
  return 0;
}
sdc_dir_t *sdc_readdir(int drive, char *name, const char *exts) {
  static sdc_dir_entry_t files[4];
  static sdc_dir_t dir = { 4, files };
  static char n0[] = "/No Disk", n1[64], n2[64], n3[64];
  assert(drive >= 0 && drive <= MAX_DRIVES);
  (void)name;
  readdir_drive = drive; readdir_exts = exts;
  char ext[8] = "dsk";
  sscanf(exts, "%7[a-z]", ext);
  files[0].name = n0; files[0].is_dir = 1;
  snprintf(n1, sizeof(n1), "GAME.%s", ext); files[1].name = n1; files[1].is_dir = 0;
  snprintf(n2, sizeof(n2), "SYSTEM.%s", ext); files[2].name = n2; files[2].is_dir = 0;
  snprintf(n3, sizeof(n3), "A rather long file name that has to scroll.%s", ext); files[3].name = n3; files[3].is_dir = 0;
  return &dir;
}

//------------------------------------------------------------------------
// the screen
//------------------------------------------------------------------------
static u8g2_t u8g2;
static const char *outdir = ".";
static int shots;
static const char *shot_prefix = "";

static void shot(const char *name) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%02d-%s-%s.txt", outdir, ++shots, shot_prefix, name);
  FILE *f = fopen(path, "w");
  if(!f) { perror(path); exit(1); }
  for(int y=0;y<64;y++) {
    for(int x=0;x<128;x++) fputc(u8x8_GetBitmapPixel(u8g2_GetU8x8(&u8g2), x, y) ? '#' : '.', f);
    fputc('\n', f);
  }
  fclose(f);
}

static int errors;
#define CHECK(cond, ...) do { if(!(cond)) { errors++; printf("FAIL [%s]: ", shot_prefix); printf(__VA_ARGS__); printf("\n"); } } while(0)

// the highlighted entry is the title or one of the four rows on screen
#define CHECK_VISIBLE(menu) CHECK((menu)->entry - (menu)->offset >= ((menu)->entry ? 1 : 0) && (menu)->entry - (menu)->offset <= 4 && \
                                  (menu)->offset >= 0 && ((menu)->entries <= 5 || (menu)->offset <= (menu)->entries - 5), \
                                  "entry %d off screen: offset %d of %d", (menu)->entry, (menu)->offset, (menu)->entries)

//------------------------------------------------------------------------
// reading a form string
//------------------------------------------------------------------------
static const char *entry_at(const char *form, int n) {
  const char *s = form;
  for(int i=0;i<n;i++) { s = strchr(s, ';'); if(!s) return NULL; s++; }
  return *s ? s : NULL;
}
static int field_int(const char *e, int n) {         // the n'th comma field as a number
  while(n--) { e = strchr(e, ','); if(!e) return -1; e++; }
  return atoi(e);
}
static const char *field(const char *e, int n) {     // the n'th comma field
  while(n--) { e = strchr(e, ','); if(!e) return NULL; e++; }
  return e;
}
static int options(const char *e) {                  // an 'L' entry's value count
  const char *v = field(e, 2);
  int n = 1;
  while(*v && *v != ';' && *v != ',') { if(*v++ == '|') n++; }
  return n;
}
static int entries(const char *form) {
  int n = 0;
  for(const char *p = form; *p && strchr(p, ';'); p = strchr(p, ';') + 1) n++;
  return n;
}
static void label(const char *e, char *buf, int len) {
  const char *l = field(e, 1);
  int n = 0;
  while(l[n] && l[n] != ',' && l[n] != ';' && n < len-1) { buf[n] = l[n]; n++; }
  buf[n] = 0;
}

//------------------------------------------------------------------------
// the walk
//------------------------------------------------------------------------
// go to entry n of the current form with DOWN from wherever we are
static void goto_entry(menu_t *menu, int n) {
  for(int i=0;i<40 && menu->entry != n;i++) { menu_do(menu, MENU_EVENT_DOWN); CHECK_VISIBLE(menu); }
  CHECK(menu->entry == n, "could not reach entry %d (at %d)", n, menu->entry);
}

static void walk_form(menu_t *menu, int form, int depth);

static void walk_entry(menu_t *menu, int form, int n, int depth) {
  const char *e = entry_at(menu->forms[form], n);
  char name[32];
  label(e, name, sizeof(name));
  goto_entry(menu, n);

  switch(e[0]) {
  case 'S': {
    int sub = field_int(e, 2);
    menu_do(menu, MENU_EVENT_SELECT);
    CHECK(menu->form == sub && menu->entry == 1, "%s: opens form %d entry %d, expected %d/1", name, menu->form, menu->entry, sub);
    walk_form(menu, sub, depth + 1);
    // the title returns to the entry that opened the form
    while(menu->entry) menu_do(menu, MENU_EVENT_UP);
    menu_do(menu, MENU_EVENT_SELECT);
    CHECK(menu->form == form && menu->entry == n, "%s: back to form %d entry %d, expected %d/%d", name, menu->form, menu->entry, form, n);
  } break;

  case 'F': {
    int slot = field_int(e, 2);
    const char *exts = strchr(field(e, 2), '|') + 1;
    readdir_drive = -1; readdir_exts = NULL;
    int sends = set_n;
    menu_do(menu, MENU_EVENT_SELECT);
    CHECK(menu->form == MENU_FORM_FSEL, "%s: selector not open, form %d", name, menu->form);
    CHECK(readdir_drive == slot, "%s: selector read slot %d, expected %d", name, readdir_drive, slot);
    CHECK(readdir_exts && !strncmp(readdir_exts, exts, strlen(exts) - 1), "%s: selector asked for '%.12s', expected '%.*s'",
          name, readdir_exts ? readdir_exts : "(null)", (int)(strchr(exts, ';') - exts), exts);
    CHECK(set_n == sends, "%s: opening a selector sent %d values", name, set_n - sends);
    if(depth == 0 && n == 1) shot("selector");
    if(slot == SDC_SLOT_EXTRA && core_id == CORE_ID_ZS256) {
      // the ROM slot: a file picked is not mounted, it goes to the core by
      // romload.c, from the core's own directory, and the OSD closes
      int opens = open_n;
      rom_selects = 0; osd_visible = 1;
      goto_entry(menu, 2);                  // GAME.rom
      menu_do(menu, MENU_EVENT_SELECT);
      char want[80];
      snprintf(want, sizeof(want), "%s/GAME.rom", ultima_root());
      CHECK(open_n == opens && rom_selects == 1 && !strcmp(rom_selected, want) && !osd_visible,
            "ROM: opened %d images, rom_select %d times with '%s' (want %s), osd %d",
            open_n - opens, rom_selects, rom_selected, want, osd_visible);
      CHECK(menu->form == form && menu->entry == n, "ROM: back to form %d entry %d", menu->form, menu->entry);
      CHECK(image_name[slot] && !strcmp(image_name[slot], "GAME.rom"), "ROM: slot %d remembers '%s'", slot, image_name[slot] ? image_name[slot] : "(null)");
      sdc_set_image_name(slot, NULL);
      menu_do(menu, MENU_EVENT_SHOW);
      break;
    }
    while(menu->entry) menu_do(menu, MENU_EVENT_UP);
    menu_do(menu, MENU_EVENT_SELECT);
    CHECK(menu->form == form && menu->entry == n, "%s: back from the selector: form %d entry %d", name, menu->form, menu->entry);
  } break;

  case 'L': {
    char id = field(e, 3)[0];
    int num = options(e);
    int start = set_last(id);
    CHECK(start >= 0, "%s: %c was never sent to the core", name, id);
    int sends = set_n;
    for(int i=0;i<num;i++) menu_do(menu, MENU_EVENT_RIGHT);
    CHECK(set_last(id) == start, "%s: %d rights leave %c at %d, started at %d", name, num, id, set_last(id), start);
    for(int i=0;i<num;i++) menu_do(menu, MENU_EVENT_LEFT);
    CHECK(set_last(id) == start, "%s: %d lefts leave %c at %d", name, num, id, set_last(id));
    CHECK(set_count(id) >= 1 + 2*num, "%s: %c sent %d times", name, id, set_count(id));
    (void)sends;
    CHECK(menu->form == form && menu->entry == n, "%s: stepping moved to form %d entry %d", name, menu->form, menu->entry);
  } break;

  case 'T': {
    int sends = set_n;
    menu_do(menu, MENU_EVENT_SELECT);
    CHECK(menu->form == MENU_FORM_TEXT && menu->offset == 0, "%s: form %d offset %d", name, menu->form, menu->offset);
    CHECK(menu_text_line(0) != NULL, "%s: an empty page", name);
    CHECK(set_n == sends, "%s: a text page sent %d values", name, set_n - sends);
    shot(name);
    menu_do(menu, MENU_EVENT_DOWN);
    menu_do(menu, MENU_EVENT_SELECT);
    CHECK(menu->form == form && menu->entry == n, "%s: back from the page: form %d entry %d", name, menu->form, menu->entry);
  } break;

  case 'B': {
    char id = field(e, 2)[0];
    if(id == 'R') {
      int r = set_count('R');
      menu_do(menu, MENU_EVENT_SELECT);
      CHECK(set_count('R') == r+2 && set_last('R') == 0 && set_log[set_n-2][1] == 1, "Reset: R sent %d more, last %d", set_count('R') - r, set_last('R'));
      CHECK(!osd_visible, "Reset left the OSD visible");
      menu_do(menu, MENU_EVENT_SHOW);
    }
    // the other buttons (Save settings, Cold Boot, Rewind) are left alone here
  } break;

  case 'I':
    CHECK(0, "%s: the info line was reached by DOWN", name);
    break;

  case 'C':
    // the Core form is checked on its own below
    break;

  default:
    CHECK(0, "%s: unknown entry type %c", name, e[0]);
  }
}

static void walk_form(menu_t *menu, int form, int depth) {
  const char *f = menu->forms[form];
  char title[32];
  int n = 0;
  while(f[n] && f[n] != ',' && n < 31) { title[n] = f[n]; n++; }
  title[n] = 0;
  shot(title);
  CHECK(menu->entries == entries(f), "%s: %d entries counted, the form has %d", title, menu->entries, entries(f));

  for(int i=1;entry_at(f, i);i++) {
    const char *e = entry_at(f, i);
    if(e[0] == 'I') continue;             // not selectable, skipped by DOWN
    walk_entry(menu, form, i, depth);
  }
}

// the Core form: on the main form, marks the running core, switches
static void check_core_form(menu_t *menu, int core_form) {
  int n;
  for(n=1;entry_at(menu->forms[0], n);n++) {
    const char *e = entry_at(menu->forms[0], n);
    if(e[0] == 'S' && field_int(e, 2) == core_form) break;
  }
  CHECK(entry_at(menu->forms[0], n) != NULL, "the main form has no Core entry for form %d", core_form);
  CHECK(!strcmp(menu->forms[core_form], core_form_ultima_text()), "form %d is not the Core form", core_form);
  goto_entry(menu, n);
  menu_do(menu, MENU_EVENT_SELECT);
  CHECK(menu->form == core_form && menu->entries == ULTIMA_CORES + 2, "Core form: form %d, %d entries", menu->form, menu->entries);
  shot("core");

  // the running core is one of the four, and selecting it does nothing
  int running = 0;
  for(int i=1;i<=ULTIMA_CORES;i++) {
    const char *e = entry_at(menu->forms[core_form], i);
    if(field_int(e, 2) == core_id) {
      running = i;
      goto_entry(menu, i);
      ultima_test_switched = 0;
      int sends = set_n;
      menu_do(menu, MENU_EVENT_SELECT);
      CHECK(!ultima_test_switched && set_n == sends && menu->form == core_form, "selecting the running core did something");
    }
  }
  CHECK(running, "the running core %02x is not on the Core form", core_id);

  // selecting another records the switch
  int other = running == 1 ? 2 : 1;
  const char *e = entry_at(menu->forms[core_form], other);
  goto_entry(menu, other);
  ultima_test_switched = 0;
  menu_do(menu, MENU_EVENT_SELECT);
  CHECK(ultima_test_switched == field_int(e, 2), "selecting core %d switched to %02x", field_int(e, 2), ultima_test_switched);
  shot("core-switch");

  // the id on every machine entry is a core
  for(int i=1;i<=ULTIMA_CORES;i++)
    CHECK(ultima_core(field_int(entry_at(menu->forms[core_form], i), 2)) != NULL, "Core entry %d names no core", i);

  // the last is "Save to flash", id 0, and selecting it saves the running core
  CHECK(field_int(entry_at(menu->forms[core_form], ULTIMA_CORES + 1), 2) == 0, "Core entry %d is not the save entry", ULTIMA_CORES + 1);
  goto_entry(menu, ULTIMA_CORES + 1);
  ultima_test_installed = 0;
  menu_do(menu, MENU_EVENT_SELECT);
  CHECK(ultima_test_installed == core_id, "Save to flash saved %02x, running %02x", ultima_test_installed, core_id);
  shot("core-save");

  // and back
  while(menu->entry) menu_do(menu, MENU_EVENT_UP);
  menu_do(menu, MENU_EVENT_SELECT);
  CHECK(menu->form == 0 && menu->entry == n, "back from Core: form %d entry %d, expected 0/%d", menu->form, menu->entry, n);
}

static void run_core(unsigned char id, const char *prefix, const char *title, int core_form) {
  core_id = id;
  shot_prefix = prefix;
  set_n = 0; open_n = 0; extrom_inits = 0; bas_runs = 0; rom_boots = 0;
  for(int i=0;i<=MAX_DRIVES;i++) { free(cwd[i]); cwd[i] = NULL; free(image_name[i]); image_name[i] = NULL; }

  menu_t *menu = menu_init(&u8g2);
  menu_do(menu, MENU_EVENT_SHOW);

  CHECK(menu->form == 0 && menu->entry == 1, "start: form %d entry %d", menu->form, menu->entry);
  CHECK(!strncmp(menu->forms[0], title, strlen(title)), "main form title: %.20s, expected %s", menu->forms[0], title);

  // the defaults went to the core once each, then the reset
  for(int i=0;menu->vars[i].id;i++)
    CHECK(set_count(menu->vars[i].id) == 1 && set_last(menu->vars[i].id) == menu->vars[i].value,
          "letter %c sent %d times at start, last %d, default %d", menu->vars[i].id, set_count(menu->vars[i].id),
          set_last(menu->vars[i].id), menu->vars[i].value);
  CHECK(set_count('R') == 2 && set_last('R') == 0, "start reset: R sent %d times, last %d", set_count('R'), set_last('R'));
  CHECK(open_n == 0, "an image was opened at start with no settings");
  CHECK(extrom_inits == (id == CORE_ID_KORVET), "extrom_init called %d times on core %02x", extrom_inits, id);
  CHECK(rom_boots == (id == CORE_ID_ZS256), "rom_boot called %d times on core %02x", rom_boots, id);

  // the settings file is the core's own, under its directory
  const ultima_core_t *c = ultima_core(id);
  char want[64];
  snprintf(want, sizeof(want), CARD_MOUNTPOINT "/%s", c->dir);
  CHECK(!strcmp(ultima_root(), want), "root is %s, expected %s", ultima_root(), want);

  walk_form(menu, 0, 0);
  check_core_form(menu, core_form);

  // every list letter is back at its default after the walk
  for(int i=0;menu->vars[i].id;i++)
    CHECK(set_last(menu->vars[i].id) == menu->vars[i].value, "letter %c ends at %d, default %d",
          menu->vars[i].id, set_last(menu->vars[i].id), menu->vars[i].value);

  menu_do(menu, MENU_EVENT_HIDE);
}

int main(int argc, char **argv) {
  if(argc > 1) outdir = argv[1];

  u8g2_SetupBitmap(&u8g2, &u8g2_cb_r0, 128, 64);
  u8x8_InitDisplay(u8g2_GetU8x8(&u8g2));

  run_core(CORE_ID_UKNC,   "uknc",   "UKNC Nano,;",   7);
  run_core(CORE_ID_PK8000, "pk8000", "PK8000 Nano,;", 2);
  run_core(CORE_ID_KORVET, "korvet", "Korvet Nano,;", 2);
  run_core(CORE_ID_ZS256,  "zs256",  "ZS-256 Nano,;", 2);

  printf("menu-test: %d screens in %s, %d error(s)\n", shots, outdir, errors);
  return errors ? 1 : 0;
}
