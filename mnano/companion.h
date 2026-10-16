// One firmware, SD-card JSON configuration. Protocol implementations stay in C.
#ifndef COMPANION_H
#define COMPANION_H

#include "menu.h"

#define COMPANION_FORMS 16
#define COMPANION_VARS 64
#define COMPANION_PATH 256

// Called before the menu or USB starts using machine configuration.
void companion_init(void);
int companion_select(unsigned char id);
int companion_configure(menu_t *menu);
void companion_rebuild(menu_t *menu);
const unsigned char *companion_keymap(void);
const unsigned char *companion_modifiers(void);
const char **companion_about(void);
const char *companion_drive_name(int drive);
int companion_save_extra(void);
int companion_reset_value(char variable);
const char *companion_core_form(void);

// All file paths go through these helpers. Relative paths resolve beneath
// a core's configured directory; /sd/... and legacy 0:/... are accepted.
// Reject traversal, ambiguous drive names and truncation instead of opening
// or creating a different file. Return 0 on success, -1 on invalid input.
int companion_path(char *out, size_t size, const char *root, const char *path);
const char *companion_resource(const char *key);

// Also used by host tests and the config-check tool. Validation is atomic:
// a rejected document does not replace the active catalog/profile.
int companion_parse_catalog(const char *json);
int companion_parse_profile(const char *json, unsigned char id);
const char *companion_error(void);

#endif
