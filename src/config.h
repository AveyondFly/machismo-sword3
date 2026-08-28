/*
 * INI-style config file parser for machismo.
 *
 * Format:
 *   [general]
 *   dylib_map = dylib_map.conf
 *   patches = patches/necrodancer.conf
 *   address_hooks = configs/sword3/address-hooks.conf
 *   entry_override = 0x100001000
 *   audit_only = true
 *
 *   [trampoline.sdl2]
 *   lib = libSDL2-2.0.so.0
 *   prefix = _SDL_
 *
 *   [trampoline.bgfx]
 *   lib = ./build-bgfx/libbgfx-shared.so
 *   prefix = _bgfx_
 *   prefix = __ZN4bgfx
 *   init_wrapper = true
 *   renderer = opengles
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define CONFIG_MAX_TRAMPOLINES 8
#define CONFIG_MAX_PREFIXES    8

typedef struct {
	char* name;                              /* section suffix, e.g. "sdl2" */
	char* lib;                               /* native .so path */
	char* prefixes[CONFIG_MAX_PREFIXES];     /* symbol prefix patterns */
	int num_prefixes;
	int init_wrapper;                        /* intercept bgfx init */
	char* renderer;                          /* force renderer type */
	char* override_lib;                      /* .so with exact-symbol replacements */
	int match_local;                         /* override pass also matches LOCAL (non-N_EXT) defined symbols */
} machismo_trampoline_config_t;

typedef struct {
	char* dylib_map;
	char* patches;
	char* splash_image;                      /* early KMS load splash PNG (relative to CWD) */
	char* address_hooks;                     /* stripped Mach-O address hook list */
	uintptr_t entry_override;                /* optional unslid guest entry vmaddr */
	char* entry_expected;                    /* expected bytes at entry_override */
	char* entry_prepare_lib;                 /* optional host pre-entry DSO */
	char* entry_prepare_symbol;              /* int prepare(void), must return 0 */
	int has_entry_override;
	int audit_only;                          /* validate/report without applying runtime changes */
	machismo_trampoline_config_t trampolines[CONFIG_MAX_TRAMPOLINES];
	int num_trampolines;
} machismo_config_t;

/* Load config from file. Returns 0 on success, -1 if file not found (not an error). */
int config_load(const char* path, machismo_config_t* cfg);

/* Free all allocated strings in config. */
void config_free(machismo_config_t* cfg);

#endif /* CONFIG_H */
