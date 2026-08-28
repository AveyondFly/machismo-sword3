#define _POSIX_C_SOURCE 200809L

#include "ports/sword3/paths.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", \
			__FILE__, __LINE__, #condition, errno); \
		goto fail; \
	} \
} while (0)

static int is_directory(const char *path)
{
	struct stat status;
	return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

int main(void)
{
	char temporary[] = "/tmp/sword3-paths-XXXXXX";
	char bundle_env[512];
	char xdg_env[512];
	char expected[512];
	char outside[512];
	char escaped[512];
	char *path = NULL;
	Sword3Paths paths = {0};
	char contents[32] = {0};
	const char first[] = "first";
	const char second[] = "atomic replacement";
	int fd = -1;
	int rc = 1;

	CHECK(mkdtemp(temporary) != NULL);
	CHECK(snprintf(bundle_env, sizeof(bundle_env), "%s//bundle/./assets/..",
		       temporary) < (int)sizeof(bundle_env));
	CHECK(snprintf(xdg_env, sizeof(xdg_env), "%s//xdg/./data", temporary) <
	      (int)sizeof(xdg_env));
	CHECK(setenv("SWORD3_BUNDLE_DIR", bundle_env, 1) == 0);
	CHECK(setenv("XDG_DATA_HOME", xdg_env, 1) == 0);
	CHECK(unsetenv("SWORD3_DATA_DIR") == 0);

	CHECK(sword3_paths_init(&paths) == 0);
	CHECK(snprintf(expected, sizeof(expected), "%s/bundle", temporary) <
	      (int)sizeof(expected));
	CHECK(strcmp(paths.bundle_dir, expected) == 0);
	CHECK(snprintf(expected, sizeof(expected), "%s/xdg/data/sword3",
		       temporary) < (int)sizeof(expected));
	CHECK(strcmp(paths.data_dir, expected) == 0);
	CHECK(is_directory(paths.data_dir));

	CHECK(sword3_paths_resource(&paths, "Video//./ch.mp4", &path) == 0);
	CHECK(snprintf(expected, sizeof(expected), "%s/bundle/Video/ch.mp4",
		       temporary) < (int)sizeof(expected));
	CHECK(strcmp(path, expected) == 0);
	free(path);
	path = NULL;

	errno = 0;
	CHECK(sword3_paths_resource(&paths, "../outside", &path) == -1);
	CHECK(errno == EPERM);
	errno = 0;
	CHECK(sword3_paths_data(&paths, "save/../outside", &path) == -1);
	CHECK(errno == EPERM);
	errno = 0;
	CHECK(sword3_paths_data(&paths, "/absolute", &path) == -1);
	CHECK(errno == EINVAL);
	errno = 0;
	CHECK(sword3_paths_atomic_write(&paths, "", first, sizeof(first) - 1,
					 0600) == -1);
	CHECK(errno == EINVAL);

	CHECK(snprintf(outside, sizeof(outside), "%s/outside", temporary) <
	      (int)sizeof(outside));
	CHECK(mkdir(outside, 0700) == 0);
	CHECK(sword3_paths_data(&paths, "linked-parent", &path) == 0);
	CHECK(symlink(outside, path) == 0);
	free(path);
	path = NULL;
	errno = 0;
	CHECK(sword3_paths_atomic_write(&paths, "linked-parent/escaped.dat",
					 first, sizeof(first) - 1, 0600) == -1);
	CHECK(errno == EPERM || errno == ELOOP || errno == ENOTDIR);
	CHECK(snprintf(escaped, sizeof(escaped), "%s/escaped.dat", outside) <
	      (int)sizeof(escaped));
	CHECK(access(escaped, F_OK) == -1 && errno == ENOENT);

	CHECK(sword3_paths_atomic_write(&paths, "profiles/one/save.dat",
					 first, sizeof(first) - 1, 0600) == 0);
	CHECK(sword3_paths_atomic_write(&paths, "profiles/one/save.dat",
					 second, sizeof(second) - 1, 0600) == 0);
	CHECK(sword3_paths_data(&paths, "profiles/one/save.dat", &path) == 0);
	fd = open(path, O_RDONLY);
	CHECK(fd >= 0);
	CHECK(read(fd, contents, sizeof(contents)) == (ssize_t)(sizeof(second) - 1));
	CHECK(memcmp(contents, second, sizeof(second) - 1) == 0);
	CHECK(close(fd) == 0);
	fd = -1;

	rc = 0;
	printf("Sword3 paths tests passed\n");

fail:
	if (fd >= 0)
		close(fd);
	free(path);
	sword3_paths_destroy(&paths);
	return rc;
}
