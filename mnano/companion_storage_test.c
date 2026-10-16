// Real FatFs on a RAM disk: exercise the production JSON reader and path users.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include "companion.h"
#include "ultima.h"
#include "sysctrl.h"
#include "rt11sav.h"
#include "romload.h"
#include "azbk.h"
#include "extrom.h"
#include "diskio.h"

// Include the actual SD image layer with only its hardware/RTOS boundary
// stubbed. Its file handles and cluster maps can then be checked on eject.
typedef void *SemaphoreHandle_t;
#define xSemaphoreCreateMutex() ((void *)1)
#define xSemaphoreTake(a,b) ((void)(a), (void)(b))
#define xSemaphoreGive(a) ((void)(a))
#include "sdc.c"

unsigned char core_id = CORE_ID_UKNC;
#define SECTORS 32768
static unsigned char *disk;
DSTATUS disk_initialize(BYTE p) { (void)p; return 0; }
DSTATUS disk_status(BYTE p) { (void)p; return 0; }
DRESULT disk_read(BYTE p, BYTE *b, LBA_t sector, UINT count) {
  (void)p;
  if(sector >= SECTORS || count > SECTORS-sector) return RES_PARERR;
  memcpy(b,disk+sector*512,count*512); return RES_OK;
}
DRESULT disk_write(BYTE p, const BYTE *b, LBA_t sector, UINT count) {
  (void)p;
  if(sector >= SECTORS || count > SECTORS-sector) return RES_PARERR;
  memcpy(disk+sector*512,b,count*512); return RES_OK;
}
DRESULT disk_ioctl(BYTE p, BYTE cmd, void *b) {
  (void)p;
  if(cmd == GET_SECTOR_COUNT) *(LBA_t *)b = SECTORS;
  else if(cmd == GET_SECTOR_SIZE) *(WORD *)b = 512;
  else if(cmd == GET_BLOCK_SIZE) *(DWORD *)b = 1;
  else if(cmd != CTRL_SYNC) return RES_PARERR;
  return RES_OK;
}
void disk_driver_callback_init(uint8_t p, FATFS_DiskioDriverTypeDef *d) { (void)p; (void)d; }
void vTaskDelay(int ms) { (void)ms; }
void spi_begin(spi_t *p) { (void)p; }
void spi_end(spi_t *p) { (void)p; }
unsigned char spi_tx_u08(spi_t *p, unsigned char v) { (void)p; (void)v; return 0; }
void sys_set_rgb(spi_t *p, unsigned long rgb) { (void)p; (void)rgb; }
void sys_handle_interrupts(unsigned char v) { (void)v; }
unsigned char sys_irq_ctrl(spi_t *p, unsigned char v) { (void)p; (void)v; return 0; }
void hid_handle_event(void) {}
static int profrom = -1;
void sys_set_val(spi_t *p, char id, uint8_t value) { (void)p; if(id == 'P') profrom = value; }
static unsigned long rom_bytes, gs_bytes;
void sys_poke24(spi_t *p, unsigned long addr, const unsigned char *buf, int len) {
  (void)p; (void)buf;
  if(addr >= ROM_ADDR_ZS && addr < ROM_ADDR_ZS+ROM_MAX_ZS) rom_bytes += len;
  else if(addr >= ROM_ADDR_GS && addr < ROM_ADDR_GS+ROM_MAX_GS) gs_bytes += len;
  else assert(0);
}
int sys_peek24(spi_t *p, unsigned long a, unsigned char *w) { (void)p; (void)a; memset(w,0,4); return 0; }
static unsigned char extrom_image[256];
void sys_extrom_status(spi_t *p, unsigned char *count, unsigned char *room, unsigned char *flags) {
  (void)p; *count = 0; *room = 64; *flags = 0;
}
void sys_extrom_write(spi_t *p, const unsigned char *buf, int n) { (void)p; (void)buf; (void)n; }
void sys_extrom_read(spi_t *p, unsigned char *buf, int n) { (void)p; memset(buf,0,n); }
void sys_extrom_flush(spi_t *p) { (void)p; }
void sys_extrom_load_rom(spi_t *p, const unsigned char *rom) { (void)p; memcpy(extrom_image,rom,256); }

static void put(const char *path, const void *data, size_t size) {
  FIL file; UINT written;
  assert(f_open(&file,path,FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
  assert(f_write(&file,data,size,&written) == FR_OK && written == size);
  assert(f_close(&file) == FR_OK);
}
static char *host_file(const char *path, size_t *size) {
  FILE *f = fopen(path,"rb"); assert(f);
  fseek(f,0,SEEK_END); *size = ftell(f); rewind(f);
  char *buf = malloc(*size+1); assert(buf);
  assert(fread(buf,1,*size,f) == *size); buf[*size] = 0; fclose(f); return buf;
}
static void copy_file(const char *source, const char *dest) {
  size_t size; char *buf = host_file(source,&size); put(dest,buf,size); free(buf);
}
static cJSON *host_json(const char *path) {
  size_t size; char *text = host_file(path,&size);
  cJSON *doc = cJSON_Parse(text); free(text); assert(doc); return doc;
}
static void put_json(const char *path, cJSON *doc) {
  char *text = cJSON_PrintUnformatted(doc); assert(text); put(path,text,strlen(text)); free(text); cJSON_Delete(doc);
}
static cJSON *item(cJSON *p, const char *key) { return cJSON_GetObjectItemCaseSensitive(p,key); }
static void replace(cJSON *p, const char *key, const char *s) {
  assert(cJSON_ReplaceItemInObjectCaseSensitive(p,key,cJSON_CreateString(s)));
}

int main(void) {
  disk = calloc(SECTORS,512); assert(disk);
  BYTE work[4096];
  MKFS_PARM options = {.fmt = FM_FAT | FM_SFD};
  assert(f_mkfs("/sd",&options,work,sizeof(work)) == FR_OK);
  assert(f_mount(&fs,"/sd",1) == FR_OK);
  assert(f_mkdir("/sd/cores") == FR_OK);

  cJSON *catalog = host_json("config/companion.json");
  replace(cJSON_GetArrayItem(item(catalog,"cores"),0),"dir","machines/uknc");
  put_json("/sd/companion.json",catalog);
  cJSON *profile = host_json("config/cores/uknc.json");
  replace(cJSON_GetArrayItem(item(profile,"forms"),0),"title","Custom UKNC");
  assert(cJSON_ReplaceItemInArray(item(item(profile,"keyboard"),"keymap"),4,cJSON_CreateNumber(77)));
  replace(item(profile,"resources"),"sav_disk","generated/BOOT.DSK");
  put_json("/sd/cores/uknc.json",profile);
  ultima_boot(NULL); // Creates both levels of the configured machine directory.
  assert(!strcmp(ultima_root(),"/sd/machines/uknc"));
  assert(!companion_select(core_id));
  menu_t menu = {0}; assert(!companion_configure(&menu));
  assert(strstr(menu.forms[0],"Custom UKNC,;"));
  assert(companion_keymap()[4] == 77);
  assert(!strcmp(companion_resource("sav_disk"),"/sd/machines/uknc/generated/BOOT.DSK"));

  // The actual FatFs SAV builder reads the configured base and writes the
  // configured output; this is not its separate stdio host implementation.
  assert(f_mkdir("/sd/machines/uknc/generated") == FR_OK);
  copy_file("../tang-uknc/soft/BASERT11.DSK","/sd/machines/uknc/BASERT11.DSK");
  copy_file("../tang-uknc/soft/RTCTST.SAV","/sd/machines/uknc/RTCTST.SAV");
  char err[64];
  assert(rt11sav_make(ultima_root(),"RTCTST.SAV",0,err,sizeof(err)) == 0);
  FILINFO info;
  assert(f_stat(companion_resource("sav_disk"),&info) == FR_OK && info.fsize == 819200);

  // Existing relative/absolute saved paths go through the real SD layer.
  sdc_set_default(0,"generated/BOOT.DSK");
  assert(!strcmp(sdc_get_cwd(0),"/sd/machines/uknc/generated"));
  assert(!sdc_image_open(0,sdc_get_image_name(0))); // Deliberate alias of stored name.
  assert(fil[0].obj.fs && lktbl[0]);
  assert(!sdc_image_open(0,NULL));
  assert(!fil[0].obj.fs && !lktbl[0] && !sdc_get_image_name(0));
  assert(!sdc_image_open(0,"/sd/machines/uknc/generated/BOOT.DSK"));
  assert(!sdc_image_open(0,NULL));
  sdc_set_default(-1,"bad"); sdc_set_default(6,"bad");
  assert(sdc_image_open(5,"bad") < 0);
  sdc_set_default(0,"/sd/uknc-old/legacy.dsk");
  assert(!strcmp(sdc_get_cwd(0),"/sd/uknc-old"));
  sdc_set_default(0,"../bad");
  assert(!strcmp(sdc_get_cwd(0),"/sd/uknc-old"));
  sdc_set_default(0,"/sd");
  assert(!strcmp(sdc_get_cwd(0),"/sd/uknc-old"));
  // An old root-level saved path cannot make the browser leave /sd, and
  // a saved directory missing from this card still gives a usable menu.
  sdc_set_default(0,"/sd/old.dsk");
  sdc_dir_t *listing = sdc_readdir(0,NULL,"dsk;");
  assert(listing->len && !strcmp(listing->files[0].name,"/No Disk"));
  sdc_readdir(0,"..","dsk;");
  assert(!strcmp(sdc_get_cwd(0),"/sd"));
  sdc_set_default(0,"/sd/missing/old.dsk");
  listing = sdc_readdir(0,NULL,"dsk;");
  assert(listing->len == 1 && !strcmp(listing->files[0].name,".."));

  // Force a fragmented image requiring more than the initial 16-word map.
  FIL fragmented, blocker; UINT written; unsigned char sector[512] = {0};
  assert(f_open(&fragmented,"/sd/FRAG.DSK",FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
  assert(f_open(&blocker,"/sd/BLOCK.DSK",FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
  for(int i = 0; i < 40; i++) {
    for(int j = 0; j < fs.csize; j++) {
      assert(f_write(&fragmented,sector,512,&written) == FR_OK && written == 512);
      assert(f_write(&blocker,sector,512,&written) == FR_OK && written == 512);
    }
  }
  assert(f_close(&fragmented) == FR_OK && f_close(&blocker) == FR_OK);
  assert(!sdc_image_open(0,"/sd/FRAG.DSK"));
  assert(fil[0].cltbl == lktbl[0] && lktbl[0][0] > 16);
  assert(!sdc_image_open(0,NULL));

  // Corrupt card JSON must recover via the same production FatFs reader.
  put("/sd/cores/uknc.json","{",1);
  assert(!companion_select(core_id)); assert(!companion_configure(&menu));
  assert(strstr(menu.forms[0],"UKNC Nano,;"));

  // The ZS loader follows custom ROM paths while retaining its upload protocol.
  assert(f_mkdir("/sd/zs256") == FR_OK); assert(f_mkdir("/sd/zs256/ROM") == FR_OK);
  profile = host_json("config/cores/zs256.json");
  replace(item(profile,"resources"),"rom","ROM/custom.rom");
  replace(item(profile,"resources"),"gs_rom","ROM/gs.rom");
  put_json("/sd/cores/zs256.json",profile);
  copy_file("../tang-zs256/soft/rom/scorp294.rom","/sd/zs256/ROM/custom.rom");
  copy_file("../tang-zs256/soft/rom/gs105a.rom","/sd/zs256/ROM/gs.rom");
  core_id = CORE_ID_ZS256; assert(!companion_select(core_id)); rom_boot(NULL);
  assert(rom_size_zs == 65536 && rom_size_gs == 32768);
  assert(rom_bytes == 65536 && gs_bytes == 32768 && profrom == 0);

  // The production ExtROM loader follows its configured root as well.
  assert(f_mkdir("/sd/korvet") == FR_OK); assert(f_mkdir("/sd/korvet/custom") == FR_OK);
  profile = host_json("config/cores/korvet.json");
  replace(item(profile,"resources"),"extrom","custom");
  put_json("/sd/cores/korvet.json",profile);
  unsigned char stage1[256]; memset(stage1,0x6c,sizeof(stage1));
  put("/sd/korvet/custom/STAGE1.ROM",stage1,sizeof(stage1));
  core_id = CORE_ID_KORVET; assert(!companion_select(core_id)); extrom_init(NULL);
  assert(!memcmp(extrom_image,stage1,sizeof(stage1)));

  // The real AZ unit layer resolves legacy 0:/ and backslash package paths.
  core_id = CORE_ID_BK; assert(!companion_select(core_id));
  assert(f_mkdir("/sd/bk") == FR_OK); assert(f_mkdir("/sd/bk/DISKS") == FR_OK);
  put("/sd/bk/DISKS/A.IMG",work,sizeof(work));
  assert(!az_set_unit(0,"0:\\DISKS\\A.IMG"));
  assert(!strcmp(az_unit_path(0),"/sd/bk/DISKS/A.IMG"));
  assert(az_unit_blocks(0) == 8);
  assert(az_set_unit(0,"../bad.img") < 0);

  puts("storage-test: JSON on FAT, relocated SAV output, SD mount/eject, fragmented maps, fallback, ZS ROM, ExtROM and BK paths passed");
  free(disk); return 0;
}
