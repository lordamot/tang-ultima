/*
  extrom_test.c - the ExtROM controller's state machine on the host
  (make extrom-test).

  extrom.c is compiled with -DEXTROM_HOST_TEST: its file layer is stdio
  on a directory that stands in for the card's extrom folder, and what
  it would send to the core's FIFO lands in extrom_out[].  This builds
  that directory (DISK/DISKA.KDI a copy of the .kdi given, STAGE2.ROM a
  synthetic 512-byte file, SYSTEM.BIN a marked one), feeds the bytes the
  machine would send, and checks the answers: the phase-1 request, the
  API's ping, a sector read against the image, a write read back, the
  name and status commands, the folder, the substitution of the system
  tracks, a bad checksum, a bad drive.

    extrom_test <dir> <image.kdi> <stage1.rom>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "extrom.h"

extern unsigned char extrom_out[];
extern int extrom_out_n;
extern void extrom_set_root(const char *);

static int errors;
#define CHECK(cond, ...) do { if(!(cond)) { errors++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while(0)

static unsigned char image[819200];
static long image_len;

static void copy_file(const char *from, const char *to) {
  FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
  if(!a || !b) { perror(from); exit(1); }
  char buf[4096]; size_t n;
  while((n = fread(buf, 1, sizeof(buf), a)) > 0) fwrite(buf, 1, n, b);
  fclose(a); fclose(b);
}

static void cmd(unsigned char c, unsigned char d, unsigned char t, unsigned char s) {
  unsigned char p[5] = { c, d, t, s, (unsigned char)(c + d + t + s - 1) };
  extrom_out_n = 0;
  extrom_feed(p, 5);
}

int main(int argc, char **argv) {
  if(argc < 4) { fprintf(stderr, "usage: extrom_test <dir> <image.kdi> <stage1.rom>\n"); return 2; }
  const char *dir = argv[1];
  char path[512];

  // the card
  mkdir(dir, 0777);
  snprintf(path, sizeof(path), "%s/DISK", dir); mkdir(path, 0777);
  snprintf(path, sizeof(path), "%s/GAMES", dir); mkdir(path, 0777);
  snprintf(path, sizeof(path), "%s/DISK/DISKA.KDI", dir); copy_file(argv[2], path);
  snprintf(path, sizeof(path), "%s/MOUNT.CFG", dir); remove(path);
  FILE *f = fopen(argv[2], "rb"); image_len = fread(image, 1, sizeof(image), f); fclose(f);
  // a phase-2 "loader": 512 bytes, load address F0h00, a marker
  snprintf(path, sizeof(path), "%s/STAGE2.ROM", dir);
  f = fopen(path, "wb");
  unsigned char st2[512]; for(int i=0;i<512;i++) st2[i] = i & 0xff; st2[6] = 0xF0;
  fwrite(st2, 1, 512, f); fclose(f);
  // a substitute system: the first two tracks, marked
  snprintf(path, sizeof(path), "%s/SYSTEM.BIN", dir);
  f = fopen(path, "wb");
  unsigned char sys[10240]; memset(sys, 0x53, sizeof(sys)); memcpy(sys, image, 32); fwrite(sys, 1, sizeof(sys), f); fclose(f);

  extrom_set_root(dir);
  extrom_reset();
  CHECK(extrom_state() == 1, "starts in phase 1 (state %d)", extrom_state());
  CHECK(!strcmp(extrom_drive_file(0), "DISKA.KDI"), "drive A defaults to DISKA.KDI (%s)", extrom_drive_file(0));
  snprintf(path, sizeof(path), "%s/MOUNT.CFG", dir);
  f = fopen(path, "rb"); CHECK(f != NULL, "MOUNT.CFG was created"); if(f) fclose(f);

  // phase 1: the loader asks for file 8
  unsigned char eight = 8;
  extrom_out_n = 0;
  extrom_feed(&eight, 1);
  CHECK(extrom_out_n == 2 + 512, "phase 2: %d bytes sent, expected 514", extrom_out_n);
  CHECK(extrom_out[0] == 0xF0 && extrom_out[1] == 2, "phase 2 header %02x %02x, expected F0 02", extrom_out[0], extrom_out[1]);
  CHECK(extrom_out_n >= 514 && !memcmp(extrom_out + 2, st2, 512), "phase 2 bytes");
  CHECK(extrom_state() == 2, "in the command state (%d)", extrom_state());

  // ping
  cmd(0, 0, 0, 0);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 1, "ping: %d bytes, %02x", extrom_out_n, extrom_out[0]);

  // a bad checksum
  { unsigned char p[5] = { 0, 0, 0, 0, 0x55 }; extrom_out_n = 0; extrom_feed(p, 5);
    CHECK(extrom_out_n == 1 && extrom_out[0] == 0, "bad crc: %d bytes, %02x", extrom_out_n, extrom_out[0]); }

  // the substitution is on: track 0 of A comes from SYSTEM.BIN
  cmd(1, 0, 0, 3);
  CHECK(extrom_out_n == 129 && extrom_out[0] == 1, "read A t0 s3: %d bytes, %02x", extrom_out_n, extrom_out[0]);
  CHECK(extrom_out[1] == 0x53, "substituted system track (got %02x)", extrom_out[1]);
  // switch it off: the image itself
  cmd(0xA0, 0, 0, 0);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 1, "A0 off");
  cmd(1, 0, 0, 3);
  CHECK(extrom_out_n == 129 && !memcmp(extrom_out + 1, image + 3 * 128, 128), "read A t0 s3 from the image");
  // a data track: (trk * 40 + sec) * 128
  cmd(1, 0, 5, 7);
  CHECK(extrom_out_n == 129 && !memcmp(extrom_out + 1, image + (5 * 40 + 7) * 128, 128), "read A t5 s7");
  // an unmounted drive
  cmd(1, 1, 0, 0);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 0, "read B (no file): %02x", extrom_out[0]);
  CHECK(!extrom_drive_mounted(1), "drive B is now unmounted");

  // write a sector, read it back
  unsigned char data[128]; for(int i=0;i<128;i++) data[i] = 0xA5 ^ i;
  cmd(2, 0, 10, 2);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 1 && extrom_state() == 3, "write A t10 s2 accepted (state %d)", extrom_state());
  extrom_out_n = 0; extrom_feed(data, 128);
  CHECK(extrom_state() == 2, "back in the command state after the data");
  cmd(1, 0, 10, 2);
  CHECK(extrom_out_n == 129 && !memcmp(extrom_out + 1, data, 128), "the written sector reads back");
  // the tool disk is read-only
  cmd(2, 4, 0, 0);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 0, "write E refused");

  // names
  cmd(0x80, 0, 0, 0);
  CHECK(extrom_out_n == 30 && extrom_out[0] == 1 && extrom_out[1] == 0 && !strcmp((char *)extrom_out + 2, "DISK") && !strcmp((char *)extrom_out + 16, "DISKA.KDI"),
        "80: %d bytes, '%s' '%s'", extrom_out_n, extrom_out + 2, extrom_out + 16);
  cmd(0x85, 0, 0, 0);
  CHECK(extrom_out_n == 15 && !strcmp((char *)extrom_out + 1, "DISK"), "85: '%s'", extrom_out + 1);
  cmd(0x82, 0, 0, 0);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 1, "82 A mounted");
  cmd(0x82, 1, 0, 0);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 0, "82 B not mounted");

  // mount B to the same file, read-only, permanently
  cmd(0x81, 1, 1, 1);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 1 && extrom_state() == 5, "81 accepted (state %d)", extrom_state());
  { unsigned char name[14]; memset(name, 0, 14); strcpy((char *)name, "DISKA.KDI"); extrom_out_n = 0; extrom_feed(name, 14); }
  CHECK(extrom_drive_mounted(1) && !strcmp(extrom_drive_file(1), "DISKA.KDI"), "B mounted to DISKA.KDI");
  cmd(1, 1, 5, 7);
  CHECK(extrom_out_n == 129 && !memcmp(extrom_out + 1, image + (5 * 40 + 7) * 128, 128), "read B t5 s7");
  cmd(2, 1, 5, 7);
  CHECK(extrom_out_n == 1 && extrom_out[0] == 0, "write B refused (read-only)");
  snprintf(path, sizeof(path), "%s/MOUNT.CFG", dir);
  f = fopen(path, "rb");
  if(f) { char cfg[128]; fread(cfg, 1, 128, f); fclose(f);
    CHECK(!strcmp(cfg + 28 + 14, "DISKA.KDI") && !strcmp(cfg + 28, "DISK"), "MOUNT.CFG holds B's mount ('%s' '%s')", cfg + 28, cfg + 42); }

  // the file list of the folder
  cmd(0x84, 0, 0, 0);
  CHECK(extrom_out_n == 1 + 14 + 1 && !strcmp((char *)extrom_out + 1, "DISKA.KDI") && extrom_out[15] == 0, "84: %d bytes, '%s'", extrom_out_n, extrom_out + 1);
  // the folders
  cmd(0x87, 0, 0, 0);
  CHECK(extrom_out_n == 1 + 2 * 14 + 1, "87: %d bytes", extrom_out_n);
  // change the folder to GAMES, then to one that does not exist
  cmd(0x86, 0, 0, 0);
  { unsigned char name[14]; memset(name, 0, 14); strcpy((char *)name, "GAMES"); extrom_out_n = 0; extrom_feed(name, 14); }
  cmd(0x85, 0, 0, 0);
  CHECK(!strcmp((char *)extrom_out + 1, "GAMES"), "85 after 86: '%s'", extrom_out + 1);
  cmd(0x86, 0, 0, 0);
  { unsigned char name[14]; memset(name, 0, 14); strcpy((char *)name, "NOPE"); extrom_out_n = 0; extrom_feed(name, 14); }
  cmd(0x85, 0, 0, 0);
  CHECK(!strcmp((char *)extrom_out + 1, "GAMES"), "a missing folder is refused: '%s'", extrom_out + 1);

  // create an image in GAMES on C, read its information sector
  cmd(0x83, 2, 0, 0);
  { unsigned char name[14]; memset(name, 0, 14); strcpy((char *)name, "NEW.KDI"); extrom_out_n = 0; extrom_feed(name, 14); }
  CHECK(extrom_drive_mounted(2) && !strcmp(extrom_drive_file(2), "NEW.KDI"), "C mounted to the new image");
  snprintf(path, sizeof(path), "%s/GAMES/NEW.KDI", dir);
  f = fopen(path, "rb"); if(f) { fseek(f, 0, SEEK_END); CHECK(ftell(f) == 819200, "the new image is %ld bytes", ftell(f)); fclose(f); } else CHECK(0, "no new image");
  cmd(1, 2, 0, 0);
  CHECK(extrom_out_n == 129 && extrom_out[1] == 0x80 && extrom_out[17] == 0x28, "the new image's information sector");

  // a reset: back to phase 1, the mounts re-read
  extrom_reset();
  CHECK(extrom_state() == 1, "reset: phase 1 again");
  CHECK(!strcmp(extrom_drive_file(1), "DISKA.KDI"), "reset: B still mounted from MOUNT.CFG ('%s')", extrom_drive_file(1));

  printf("extrom-test: %d error(s)\n", errors);
  return errors ? 1 : 0;
}
