/* JSON is the configuration source for all five companion machines.
 * Parse one active profile, retaining its strings until the MCU restarts.
 * Card overrides are validated completely before they replace defaults. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <ff.h>
#include <cJSON.h>
#include "companion.h"
#include "ultima.h"
#include "sysctrl.h"
#include "companion_defaults.h"

#define JSON_MAX 32768
#define FORM_MAX 4096
#define ABOUT_MAX 48

typedef struct {
  cJSON *doc, *resources, *reset;
  const char *forms[COMPANION_FORMS];
  menu_variable_t vars[COMPANION_VARS + 1];
  const char *about[ABOUT_MAX + 1], *drives[MAX_DRIVES];
  unsigned char keys[256], mods[8];
  int nforms, nvars, extra, mapped, id;
} profile_t;

ultima_core_t ultima_cores[ULTIMA_CORES];
static profile_t active;
static int initialized;
static char error_text[160];
static char core_form[512];
static char form_buffer[FORM_MAX];
static const unsigned char supported_ids[] = {5, 7, 8, 9, 10, 11};
static const char *const protocols[] = {"uknc", "pk8000", "korvet", "zs256", "bk", "evo"};

static int fail(const char *why) {
  snprintf(error_text, sizeof(error_text), "%s", why);
  return -1;
}
const char *companion_error(void) { return error_text; }
static cJSON *get(cJSON *obj, const char *key) { return cJSON_GetObjectItemCaseSensitive(obj, key); }
static int number(cJSON *v, int low, int high) {
  return cJSON_IsNumber(v) && v->valuedouble == v->valueint && v->valueint >= low && v->valueint <= high;
}
static int string(cJSON *v, int max, const char *forbidden) {
  if(!cJSON_IsString(v) || strlen(v->valuestring) > (size_t)max) return 0;
  for(const unsigned char *p = (void *)v->valuestring; *p; p++)
    if(*p < 32 || (forbidden && strchr(forbidden, *p))) return 0;
  return 1;
}
static int letter(cJSON *v) { return string(v, 1, ",;|=\\") && strlen(v->valuestring) == 1 && v->valuestring[0] > 32; }

// Reject duplicate keys, which otherwise make an edit depend on parser order.
static int unique(cJSON *v) {
  if(cJSON_IsObject(v))
    for(cJSON *a = v->child; a; a = a->next)
      for(cJSON *b = a->next; b; b = b->next)
        if(!strcmp(a->string, b->string)) return 0;
  for(cJSON *a = v->child; a; a = a->next) if(!unique(a)) return 0;
  return 1;
}

static cJSON *parse(const char *text) {
  // Bound nesting before cJSON recurses on the OSD task's stack.
  int depth = 0, quoted = 0, escape = 0;
  size_t len = strlen(text);
  if(len > JSON_MAX) { fail("JSON exceeds 32 KB"); return NULL; }
  for(size_t i = 0; i < len; i++) {
    char c = text[i];
    if(quoted) {
      if(escape) { escape = 0; if(c == 'u' && !strncmp(text+i+1, "0000", 4)) { fail("NUL in JSON string"); return NULL; } }
      else if(c == '\\') escape = 1;
      else if(c == '"') quoted = 0;
    } else if(c == '"') quoted = 1;
    else if(c == '[' || c == '{') { if(++depth > 12) { fail("JSON nesting exceeds 12"); return NULL; } }
    else if(c == ']' || c == '}') depth--;
  }
  cJSON *doc = cJSON_ParseWithOpts(text, NULL, 1);
  if(!doc || !cJSON_IsObject(doc) || !unique(doc)) {
    cJSON_Delete(doc); fail("invalid JSON object or duplicate key"); return NULL;
  }
  return doc;
}

int companion_path(char *out, size_t size, const char *root, const char *path) {
  char full[COMPANION_PATH];
  int n;
  if(!out || !size || !root || !path || !*path) return -1;
  // Legacy FatFs paths are relative to the machine, not the card root.
  if(!strncmp(path, "0:", 2)) path += 2;
  if(!strncasecmp(path, CARD_MOUNTPOINT "/", strlen(CARD_MOUNTPOINT) + 1) || !strcasecmp(path, CARD_MOUNTPOINT))
    n = snprintf(full, sizeof(full), CARD_MOUNTPOINT "%s", path + strlen(CARD_MOUNTPOINT));
  else {
    while(*path == '/' || *path == '\\') path++;
    n = snprintf(full, sizeof(full), "%s/%s", root, path);
  }
  if(n < 0 || (size_t)n >= sizeof(full) || (size_t)n >= size) return -1;
  for(char *p = full; *p; p++) {
    if(*p == '\\') *p = '/';
    if((unsigned char)*p < 32 || *p == ':') return -1;
  }
  if(strncmp(full, CARD_MOUNTPOINT, strlen(CARD_MOUNTPOINT)) ||
     (full[strlen(CARD_MOUNTPOINT)] && full[strlen(CARD_MOUNTPOINT)] != '/')) return -1;
  for(char *p = full; *p;) {
    while(*p == '/') p++;
    char *end = strchr(p, '/');
    size_t len = end ? (size_t)(end-p) : strlen(p);
    if((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return -1;
    p += len;
  }
  strcpy(out, full);
  return 0;
}

static int relative(cJSON *v, int max) {
  char path[COMPANION_PATH];
  return string(v, max, ",;|:\\") && *v->valuestring && v->valuestring[0] != '/' &&
    v->valuestring[strlen(v->valuestring)-1] != '/' && !strstr(v->valuestring,"//") &&
    !companion_path(path, sizeof(path), CARD_MOUNTPOINT, v->valuestring);
}

static int append(char *buf, size_t cap, const char *fmt, ...) {
  size_t used = strlen(buf);
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf + used, cap - used, fmt, args);
  va_end(args);
  return n >= 0 && (size_t)n < cap - used ? 0 : -1;
}

static void build_core_form(void) {
  strcpy(core_form, "Core,0|1;");
  for(int i = 0; i < ULTIMA_CORES; i++)
    append(core_form, sizeof(core_form), "C,%s,%u;", ultima_cores[i].name, ultima_cores[i].id);
  append(core_form, sizeof(core_form), "C,Save to flash,0;");
}

int companion_parse_catalog(const char *json) {
  cJSON *doc = parse(json);
  if(!doc) return -1;
  ultima_core_t next[ULTIMA_CORES];
  int ok = number(get(doc, "version"), 1, 1);
  cJSON *cores = get(doc, "cores");
  ok = ok && cJSON_IsArray(cores) && cJSON_GetArraySize(cores) == ULTIMA_CORES;
  memset(next, 0, sizeof(next));
  for(int i = 0; ok && i < ULTIMA_CORES; i++) {
    cJSON *c = cJSON_GetArrayItem(cores, i);
    ok = number(get(c,"id"), supported_ids[i], supported_ids[i]) &&
      string(get(c,"name"), 31, ",;|") && *get(c,"name")->valuestring &&
      relative(get(c,"dir"), 79) && relative(get(c,"settings"), 79) &&
      relative(get(c,"bitstream"), 119) && relative(get(c,"profile"), 119);
    if(!ok) break;
    next[i].id = supported_ids[i];
    strcpy(next[i].name, get(c,"name")->valuestring);
    strcpy(next[i].dir, get(c,"dir")->valuestring);
    strcpy(next[i].ini, get(c,"settings")->valuestring);
    strcpy(next[i].bitstream, get(c,"bitstream")->valuestring);
    strcpy(next[i].profile, get(c,"profile")->valuestring);
    char root[COMPANION_PATH], path[COMPANION_PATH];
    ok = !companion_path(root, sizeof(root), CARD_MOUNTPOINT, next[i].dir) &&
         !companion_path(path, sizeof(path), root, next[i].ini);
    for(int j = 0; j < i; j++)
      if(!strcasecmp(next[i].dir,next[j].dir) || !strcasecmp(next[i].bitstream,next[j].bitstream) ||
         !strcasecmp(next[i].profile,next[j].profile)) ok = 0;
  }
  cJSON_Delete(doc);
  if(!ok) return fail("invalid catalog: version, core order, duplicate or invalid paths");
  memcpy(ultima_cores, next, sizeof(next));
  build_core_form();
  return 0;
}

static void release(profile_t *p) {
  for(int i = 0; i < COMPANION_FORMS; i++) free((void *)p->forms[i]);
  cJSON_Delete(p->doc);
  memset(p, 0, sizeof(*p));
}
static int var_index(profile_t *p, char id) {
  for(int i = 0; i < p->nvars; i++) if(p->vars[i].id == id) return i;
  return -1;
}

static int join(cJSON *array, char *buf, size_t size, const char *sep) {
  if(!cJSON_IsArray(array) || cJSON_GetArraySize(array) < 1 || cJSON_GetArraySize(array) > 256) return -1;
  int i = 0;
  cJSON *v;
  cJSON_ArrayForEach(v, array) {
    if(!string(v, 128, ",;|") || !*v->valuestring || append(buf, size, "%s%s", i++ ? sep : "", v->valuestring)) return -1;
  }
  return 0;
}

static int compile_form(profile_t *p, int index, int show_all) {
  cJSON *forms = get(p->doc, "forms"), *f = cJSON_GetArrayItem(forms, index);
  cJSON *parent = get(f,"parent"), *entries = get(f,"entries");
  if(!string(get(f,"title"), 48, ",;|") || !*get(f,"title")->valuestring ||
     !cJSON_IsArray(entries) || cJSON_GetArraySize(entries) > 64 ||
     (index == 0 ? !cJSON_IsNull(parent) : !number(parent, 0, p->nforms-1))) return -1;
  form_buffer[0] = 0;
  if(index == 0) append(form_buffer, sizeof(form_buffer), "%s,;", get(f,"title")->valuestring);
  else append(form_buffer, sizeof(form_buffer), "%s,%d|1;", get(f,"title")->valuestring, parent->valueint);
  cJSON *e;
  cJSON_ArrayForEach(e, entries) {
    cJSON *t = get(e,"type"), *label = get(e,"label"), *cond = get(e,"visible_if");
    if(!string(t, 16, NULL)) return -1;
    if(!strcmp(t->valuestring, "cores")) {
      if(index == 0 || cJSON_GetArraySize(entries) != 1 || parent->valueint != 0) return -1;
      snprintf(form_buffer, sizeof(form_buffer), "%s", core_form);
      break;
    }
    if(!string(label, 48, ",;|")) return -1;
    int visible = 1;
    if(cond) {
      if(!letter(cond)) return -1;
      int v = var_index(p, cond->valuestring[0]);
      if(v < 0) return -1;
      visible = p->vars[v].value > 0;
    }
    char line[2048] = "";
    const char *type = t->valuestring, *lbl = label->valuestring;
    if(!strcmp(type,"submenu")) {
      cJSON *target = get(e,"form");
      if(!number(target, 1, p->nforms-1) || !number(get(cJSON_GetArrayItem(forms,target->valueint),"parent"),index,index)) return -1;
      append(line, sizeof(line), "S,%s,%d;", lbl, target->valueint);
    } else if(!strcmp(type,"file")) {
      cJSON *slot = get(e,"slot");
      if(!number(slot,0,MAX_DRIVES)) return -1;
      append(line,sizeof(line),"F,%s,%d|",lbl,slot->valueint);
      if(join(get(e,"extensions"),line,sizeof(line),"+") || append(line,sizeof(line),";")) return -1;
    } else if(!strcmp(type,"list")) {
      cJSON *v = get(e,"variable"), *opts = get(e,"options");
      if(!letter(v)) return -1;
      int vi = var_index(p,v->valuestring[0]);
      if(vi < 0 || p->vars[vi].value >= cJSON_GetArraySize(opts)) return -1;
      append(line,sizeof(line),"L,%s,",lbl);
      if(join(opts,line,sizeof(line),"|") || append(line,sizeof(line),",%s;",v->valuestring)) return -1;
    } else if(!strcmp(type,"button")) {
      if(!letter(get(e,"action"))) return -1;
      append(line,sizeof(line),"B,%s,%s;",lbl,get(e,"action")->valuestring);
    } else if(!strcmp(type,"text") || !strcmp(type,"info")) {
      if(!strcmp(type,"info") && (p->id != CORE_ID_UKNC || strcmp(lbl,"rtc"))) return -1;
      if(!strcmp(type,"text") && strcmp(lbl,"About") && strcmp(lbl,"Debug")) return -1;
      append(line,sizeof(line),"%c,%s,;",type[0] == 't' ? 'T' : 'I',lbl);
    } else return -1;
    if((visible || show_all) && append(form_buffer,sizeof(form_buffer),"%s",line)) return -1;
  }
  // Reserve enough space for hidden entries as well, so rebuild never allocates
  // while USB and interrupt handlers use the active configuration.
  if(show_all) {
    p->forms[index] = malloc(strlen(form_buffer)+1);
    if(!p->forms[index]) return -1;
  }
  strcpy((char *)p->forms[index], form_buffer);
  return 0;
}

int companion_parse_profile(const char *json, unsigned char id) {
  profile_t p = {0};
  int which = -1;
  for(int i = 0; i < ULTIMA_CORES; i++) if(supported_ids[i] == id) which = i;
  if(which < 0) return fail("unsupported core ID");
  p.doc = parse(json);
  if(!p.doc) return -1;
  p.id = id;
  cJSON *forms = get(p.doc,"forms"), *defs = get(p.doc,"defaults"), *keys = get(p.doc,"keyboard");
  cJSON *v, *proto = get(p.doc,"protocol");
  if(!number(get(p.doc,"version"),1,1) || !number(get(p.doc,"id"),id,id) ||
     !cJSON_IsString(proto) || strcmp(proto->valuestring, protocols[which]) ||
     !cJSON_IsArray(forms) || (p.nforms = cJSON_GetArraySize(forms)) < 2 || p.nforms > COMPANION_FORMS ||
     !cJSON_IsObject(defs) || !cJSON_IsObject(keys)) goto invalid;
  cJSON_ArrayForEach(v, defs) {
    if(p.nvars >= COMPANION_VARS || strlen(v->string) != 1 || !number(v,0,255) ||
       !isgraph((unsigned char)v->string[0]) || strchr(",;|=\\",v->string[0])) goto invalid;
    p.vars[p.nvars].id = v->string[0];
    p.vars[p.nvars++].value = v->valueint;
  }
  int core_forms = 0;
  for(int i = 0; i < p.nforms; i++) {
    if(compile_form(&p,i,1)) goto invalid;
    cJSON *entries = get(cJSON_GetArrayItem(forms,i),"entries");
    cJSON_ArrayForEach(v,entries) if(!strcmp(get(v,"type")->valuestring,"cores")) core_forms++;
    // Every parent chain must end at the main form. This also rejects cycles.
    int at = i, steps = 0;
    while(at && steps++ < p.nforms) {
      cJSON *parent = get(cJSON_GetArrayItem(forms,at),"parent");
      if(!number(parent,0,p.nforms-1)) goto invalid;
      int child = at;
      at = parent->valueint;
      int linked = 0;
      cJSON *e;
      cJSON_ArrayForEach(e,get(cJSON_GetArrayItem(forms,at),"entries")) {
        cJSON *t = get(e,"type");
        if(cJSON_IsString(t) && !strcmp(t->valuestring,"submenu") && number(get(e,"form"),child,child)) linked = 1;
      }
      if(!linked) goto invalid;
    }
    if(at) goto invalid;
  }
  if(core_forms != 1) goto invalid;
  cJSON *driver = get(keys,"driver");
  const char *expected = id == CORE_ID_UKNC ? "uknc" : id == CORE_ID_BK ? "bk" : "ps2";
  if(!cJSON_IsString(driver) || strcmp(driver->valuestring,expected)) goto invalid;
  p.mapped = id != CORE_ID_BK;
  if(p.mapped) {
    cJSON *map = get(keys,"keymap"), *mods = get(keys,"modifiers");
    if(!cJSON_IsArray(map) || cJSON_GetArraySize(map) > 256 || cJSON_GetArraySize(map) < 1 ||
       !cJSON_IsArray(mods) || cJSON_GetArraySize(mods) != 8) goto invalid;
    for(int i = 0; i < cJSON_GetArraySize(map); i++) {
      v = cJSON_GetArrayItem(map,i);
      if(!number(v,0,255)) goto invalid;
      p.keys[i] = v->valueint;
    }
    for(int i = 0; i < 8; i++) {
      v = cJSON_GetArrayItem(mods,i);
      if(!number(v,0,255)) goto invalid;
      p.mods[i] = v->valueint;
    }
  }
  cJSON *drives = get(p.doc,"drive_names"), *about = get(p.doc,"about"), *extra = get(p.doc,"save_extra_slot");
  p.resources = get(p.doc,"resources");
  p.reset = get(p.doc,"reset_on_change");
  if(!cJSON_IsArray(drives) || cJSON_GetArraySize(drives) != MAX_DRIVES ||
     !cJSON_IsArray(about) || cJSON_GetArraySize(about) > ABOUT_MAX ||
     !cJSON_IsObject(p.resources) || !cJSON_IsObject(p.reset) || !cJSON_IsBool(extra)) goto invalid;
  for(int i = 0; i < MAX_DRIVES; i++) {
    v = cJSON_GetArrayItem(drives,i);
    if(!string(v,24,NULL)) goto invalid;
    p.drives[i] = v->valuestring;
  }
  for(int i = 0; i < cJSON_GetArraySize(about); i++) {
    v = cJSON_GetArrayItem(about,i);
    if(!string(v,1024,NULL)) goto invalid;
    p.about[i] = v->valuestring;
  }
  cJSON_ArrayForEach(v,p.resources) if(!relative(v,119)) goto invalid;
  const char *const required[][6] = {{"sav_base","sav_base_alt","sav_disk",NULL}, {NULL}, {"extrom",NULL}, {"rom","gs_rom",NULL}, {"az_ini","az_eeprom",NULL}, {"rom","gs_rom","ngs_rom","cmos","sd",NULL}};
  for(int i = 0; required[which][i]; i++) if(!get(p.resources,required[which][i])) goto invalid;
  cJSON_ArrayForEach(v,p.reset)
    if(strlen(v->string) != 1 || var_index(&p,v->string[0]) < 0 || !number(v,1,3)) goto invalid;
  p.extra = cJSON_IsTrue(extra);
  if(p.extra && id != CORE_ID_ZS256 && id != CORE_ID_EVO) goto invalid;
  release(&active);
  active = p;
  for(int i = 0; i < active.nforms; i++) compile_form(&active,i,0);
  return 0;
invalid:
  release(&p);
  return fail("invalid profile: identity, menu, defaults, keyboard or paths");
}

#ifndef COMPANION_HOST
static char *read_card(const char *path) {
  FIL file;
  char *buf = NULL;
  sdc_lock();
  if(f_open(&file,path,FA_READ | FA_OPEN_EXISTING) == FR_OK) {
    size_t size = f_size(&file);
    if(size && size <= JSON_MAX) {
      buf = malloc(size+1);
      UINT got;
      if(buf && (f_read(&file,buf,size,&got) != FR_OK || got != size)) { free(buf); buf = NULL; }
      if(buf) buf[size] = 0;
    } else printf("companion: %s exceeds the size limit or is empty\r\n",path);
    f_close(&file);
  }
  sdc_unlock();
  return buf;
}
#else
// Host tests read the real card-layout files, without a simulated SD controller.
static char *read_card(const char *path) {
  const char *root = getenv("COMPANION_CARD");
  if(!root) return NULL;
  char full[512];
  snprintf(full,sizeof(full),"%s%s",root,path+strlen(CARD_MOUNTPOINT));
  FILE *f = fopen(full,"rb");
  if(!f) return NULL;
  fseek(f,0,SEEK_END); long n = ftell(f); rewind(f);
  char *buf = n > 0 && n <= JSON_MAX ? malloc(n+1) : NULL;
  if(buf) { if(fread(buf,1,n,f) != (size_t)n) { free(buf); buf = NULL; } else buf[n] = 0; }
  fclose(f); return buf;
}
#endif

void companion_init(void) {
  if(initialized) return;
  if(companion_parse_catalog(companion_defaults[0])) { printf("companion: embedded catalog invalid\r\n"); return; }
  char *json = read_card(CARD_MOUNTPOINT "/companion.json");
  if(json) {
    if(companion_parse_catalog(json)) printf("companion.json: %s; using embedded catalog\r\n",companion_error());
    free(json);
  }
  initialized = 1;
}
int companion_select(unsigned char id) {
  companion_init();
  int which = -1;
  for(int i = 0; i < ULTIMA_CORES; i++) if(ultima_cores[i].id == id) which = i;
  if(which < 0) return fail("unsupported core ID");
  char path[COMPANION_PATH];
  char *json = NULL;
  if(!companion_path(path,sizeof(path),CARD_MOUNTPOINT,ultima_cores[which].profile)) json = read_card(path);
  if(json) {
    int r = companion_parse_profile(json,id);
    free(json);
    if(!r) { printf("companion: profile %s\r\n",path); return 0; }
    printf("companion: %s: %s; using embedded profile\r\n",path,companion_error());
  }
  printf("companion: embedded %s profile\r\n", protocols[which]);
  return companion_parse_profile(companion_defaults[which+1],id);
}
int companion_configure(menu_t *menu) {
  if(active.id != core_id && companion_select(core_id)) return -1;
  menu->vars = active.vars; menu->forms = active.forms;
  return 0;
}
void companion_rebuild(menu_t *menu) {
  if(menu->vars != active.vars) return;
  // A hand-edited old INI must not index beyond a JSON list's options.
  cJSON *f, *e;
  cJSON_ArrayForEach(f,get(active.doc,"forms")) {
    cJSON_ArrayForEach(e,get(f,"entries")) {
      cJSON *type = get(e,"type");
      if(!strcmp(type->valuestring,"list")) {
        int i = var_index(&active,get(e,"variable")->valuestring[0]);
        if(i >= 0 && (active.vars[i].value < 0 || active.vars[i].value >= cJSON_GetArraySize(get(e,"options"))))
          active.vars[i].value = get(get(active.doc,"defaults"),get(e,"variable")->valuestring)->valueint;
      }
    }
  }
  for(int i = 0; i < active.nforms; i++) compile_form(&active,i,0);
}
const unsigned char *companion_keymap(void) { return active.mapped ? active.keys : NULL; }
const unsigned char *companion_modifiers(void) { return active.mapped ? active.mods : NULL; }
const char **companion_about(void) { return active.about; }
const char *companion_drive_name(int drive) { return drive >= 0 && drive < MAX_DRIVES && active.drives[drive] ? active.drives[drive] : "-"; }
int companion_save_extra(void) { return active.extra; }
int companion_reset_value(char variable) { char key[] = {variable,0}; cJSON *v = get(active.reset,key); return v ? v->valueint : 0; }
const char *companion_core_form(void) { return core_form; }
const char *companion_resource(const char *key) {
  static char path[COMPANION_PATH];
  cJSON *v = get(active.resources,key);
  if(!cJSON_IsString(v) || companion_path(path,sizeof(path),ultima_root(),v->valuestring)) return "";
  return path;
}
