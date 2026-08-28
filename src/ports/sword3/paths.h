#ifndef SWORD3_PATHS_H
#define SWORD3_PATHS_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sword3Paths {
	char *bundle_dir;
	char *data_dir;
	/*
	 * Private directory handle anchoring persistent writes. Keep this
	 * structure zero-initialized and do not copy it after initialization.
	 */
	int data_dir_fd;
} Sword3Paths;

/*
 * SWORD3_BUNDLE_DIR must name an absolute resource directory.
 * Persistent data uses SWORD3_DATA_DIR, then $XDG_DATA_HOME/sword3, then
 * $HOME/.local/share/sword3.  All configured roots are normalized.
 */
int sword3_paths_init(Sword3Paths *paths);
void sword3_paths_destroy(Sword3Paths *paths);

/*
 * Join a root with a bundle-relative path. The returned string is allocated
 * with malloc and must be freed by the caller. Absolute paths and every ".."
 * component are rejected. These helpers perform lexical joining; use
 * sword3_paths_atomic_write for symlink-safe persistent writes.
 */
int sword3_paths_resource(const Sword3Paths *paths, const char *relative,
			  char **result);
int sword3_paths_data(const Sword3Paths *paths, const char *relative,
		      char **result);

/* Recursively create an absolute directory path. */
int sword3_paths_mkdirs(const char *path, mode_t mode);

/*
 * Atomically replace a persistent-data file using a temporary file and
 * renameat(2). Missing parent directories are created below the directory
 * handle captured by sword3_paths_init. Symlinked relative components and
 * empty target names are rejected.
 */
int sword3_paths_atomic_write(const Sword3Paths *paths, const char *relative,
			      const void *data, size_t size, mode_t mode);

#ifdef __cplusplus
}
#endif

#endif
