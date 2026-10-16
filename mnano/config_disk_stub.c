// Parser/path tests have no mounted volume. Keep real FatFs linked so
// ultima.c's host implementation is exactly the menu-test implementation.
#include "diskio.h"
DSTATUS disk_initialize(BYTE p) { (void)p; return STA_NOINIT; }
DSTATUS disk_status(BYTE p) { (void)p; return STA_NOINIT; }
DRESULT disk_read(BYTE p, BYTE *b, LBA_t s, UINT c) { (void)p; (void)b; (void)s; (void)c; return RES_NOTRDY; }
DRESULT disk_write(BYTE p, const BYTE *b, LBA_t s, UINT c) { (void)p; (void)b; (void)s; (void)c; return RES_NOTRDY; }
DRESULT disk_ioctl(BYTE p, BYTE c, void *b) { (void)p; (void)c; (void)b; return RES_NOTRDY; }
