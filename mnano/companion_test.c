// Exercise the real parser, menu compiler and path resolver on the host.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "companion.h"
#include "ultima.h"
#include "sysctrl.h"

unsigned char core_id = CORE_ID_UKNC;
void sdc_lock(void) {}
void sdc_unlock(void) {}

static char *read_file(const char *name) {
  FILE *f = fopen(name,"rb");
  if(!f) return NULL;
  fseek(f,0,SEEK_END); long n = ftell(f); rewind(f);
  if(n < 0 || n > 65536) { fclose(f); return NULL; }
  char *text = malloc(n+1);
  assert(text);
  assert(fread(text,1,n,f) == (size_t)n);
  text[n] = 0; fclose(f); return text;
}

int main(int argc, char **argv) {
  companion_init();
  if(argc == 3 && !strcmp(argv[1],"boot")) {
    core_id = atoi(argv[2]);
    assert(!companion_select(core_id));
    menu_t menu = {0};
    assert(!companion_configure(&menu));
    puts(menu.forms[0]);
    if(companion_keymap()) printf("key4=%u\n",companion_keymap()[4]);
    if(core_id == CORE_ID_UKNC) printf("sav=%s\n",companion_resource("sav_disk"));
    return 0;
  }
  if(argc == 3 && !strcmp(argv[1],"catalog")) {
    char *text = read_file(argv[2]);
    int r = text ? companion_parse_catalog(text) : -1;
    free(text);
    if(r) { fprintf(stderr,"%s\n",companion_error()); return 1; }
    for(int i = 0; i < ULTIMA_CORES; i++) {
      core_id = ultima_cores[i].id;
      printf("%u %s %s\n",core_id,ultima_root(),ultima_core_path(&ultima_cores[i]));
    }
    return 0;
  }
  if(argc == 4 && !strcmp(argv[1],"profile")) {
    core_id = atoi(argv[2]);
    assert(!companion_select(core_id));
    const unsigned char *old_keys = companion_keymap();
    unsigned old_key = old_keys ? old_keys[4] : 0;
    menu_t menu = {0};
    assert(!companion_configure(&menu));
    const char *old_form = menu.forms[0];
    char *text = read_file(argv[3]);
    int r = text ? companion_parse_profile(text,core_id) : -1;
    free(text);
    if(r) {
      // Rejected documents must preserve the usable configuration.
      assert(!companion_configure(&menu) && menu.forms[0] == old_form);
      assert(!old_keys || companion_keymap()[4] == old_key);
      fprintf(stderr,"%s\n",companion_error()); return 1;
    }
    assert(!companion_configure(&menu));
    puts(menu.forms[0]);
    if(companion_keymap()) printf("key4=%u\n",companion_keymap()[4]);
    if(core_id == CORE_ID_UKNC) {
      printf("sav=%s\n",companion_resource("sav_disk"));
      for(int i = 0; menu.vars[i].id; i++) {
        if(menu.vars[i].id == 'e') menu.vars[i].value = 0;
        if(menu.vars[i].id == 'A') menu.vars[i].value = 100000;
      }
      companion_rebuild(&menu);
      assert(!strstr(menu.forms[0],"F,HDD0:"));
      for(int i = 0; menu.vars[i].id; i++) if(menu.vars[i].id == 'e') menu.vars[i].value = 1;
      companion_rebuild(&menu);
      assert(strstr(menu.forms[0],"F,HDD0:"));
      puts("conditional HDD entry and saved-value bounds: ok");
    }
    return 0;
  }
  if(argc == 5 && !strcmp(argv[1],"path")) {
    char path[COMPANION_PATH];
    int size = atoi(argv[4]);
    if(size < 1 || (size_t)size > sizeof(path)) return 2;
    int r = companion_path(path,size,argv[2],argv[3]);
    if(!r) puts(path);
    return r ? 1 : 0;
  }
  fprintf(stderr,"usage: companion_test catalog FILE | profile ID FILE | path ROOT PATH SIZE\n");
  return 2;
}
