//
// bas_test.c - the firmware's tokeniser on the host: a .bas file in, the
// program as it would sit at 4001h out, for `make bas-test` to compare
// with tools/mkcas.py --tok (the two must produce the same bytes).
//
//   bas_test in.bas out.tok
//
#include <stdio.h>
#include <stdlib.h>
#include "bas.h"

int main(int argc, char **argv) {
  if(argc != 3) { fprintf(stderr, "bas_test in.bas out.tok\n"); return 1; }
  FILE *in = fopen(argv[1], "r"), *out = fopen(argv[2], "wb");
  if(!in || !out) { perror("bas_test"); return 1; }
  char text[256];
  unsigned char line[256];
  unsigned addr = 0x4001;
  int lines = 0;
  while(fgets(text, sizeof(text), in)) {
    unsigned number;
    int n = bas_tokenise(text, line + 4, 250, &number);
    if(n < 0) continue;
    unsigned next = addr + 4 + n + 1;
    line[0] = next & 0xff; line[1] = next >> 8;
    line[2] = number & 0xff; line[3] = number >> 8;
    line[4 + n] = 0;
    fwrite(line, 1, 4 + n + 1, out);
    addr = next;
    lines++;
  }
  fputc(0, out); fputc(0, out);
  fclose(out);
  printf("%s: %d lines, %u bytes\n", argv[2], lines, addr + 2 - 0x4001);
  return 0;
}
