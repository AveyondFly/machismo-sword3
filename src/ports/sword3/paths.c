#define _POSIX_C_SOURCE 200809L

#include "paths.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int normalize_absolute(const char *path, char **result)
{
	char *copy;
	char *out;
	char *save = NULL;
	char *part;
	size_t length;

	if (!path || path[0] != '/' || !result) {
		errno = EINVAL;
		return -1;
	}

	length = strlen(path);
	if (length > SIZE_MAX - 2) {
		errno = ENAMETOOLONG;
		return -1;
	}
	copy = strdup(path);
	out = malloc(length + 2);
	if (!copy || !out) {
		free(copy);
		free(out);
		errno = ENOMEM;
		return -1;
	}

	out[0] = '/';
	out[1] = '\0';
	length = 1;
	for (part = strtok_r(copy, "/", &save); part;
	     part = strtok_r(NULL, "/", &save)) {
		size_t part_len;

		if (!strcmp(part, ".") || !*part)
			continue;
		if (!strcmp(part, "..")) {
			if (length > 1) {
				while (length > 1 && out[length - 1] != '/')
					length--;
				if (length > 1)
					length--;
				out[length] = '\0';
			}
			continue;
		}

		part_len = strlen(part);
		if (length > 1)
			out[length++] = '/';
		memcpy(out + length, part, part_len);
		length += part_len;
		out[length] = '\0';
	}

	free(copy);
	*result = out;
	return 0;
}

static int join_relative(const char *root, const char *relative, char **result)
{
	const char *cursor;
	size_t root_len;
	size_t relative_len;
	size_t needed;
	char *joined;
	size_t used;

	if (!root || !relative || !result || relative[0] == '/') {
		errno = EINVAL;
		return -1;
	}

	cursor = relative;
	while (*cursor) {
		const char *end = strchr(cursor, '/');
		size_t len = end ? (size_t)(end - cursor) : strlen(cursor);

		if (len == 2 && cursor[0] == '.' && cursor[1] == '.') {
			errno = EPERM;
			return -1;
		}
		cursor = end ? end + 1 : cursor + len;
	}

	root_len = strlen(root);
	relative_len = strlen(relative);
	if (relative_len > SIZE_MAX - 2 ||
	    root_len > SIZE_MAX - relative_len - 2) {
		errno = ENAMETOOLONG;
		return -1;
	}
	needed = root_len + relative_len + 2;
	joined = malloc(needed);
	if (!joined) {
		errno = ENOMEM;
		return -1;
	}

	memcpy(joined, root, root_len);
	used = root_len;
	cursor = relative;
	while (*cursor) {
		const char *end = strchr(cursor, '/');
		size_t len = end ? (size_t)(end - cursor) : strlen(cursor);

		if (len && !(len == 1 && cursor[0] == '.')) {
			if (used > 1)
				joined[used++] = '/';
			memcpy(joined + used, cursor, len);
			used += len;
		}
		cursor = end ? end + 1 : cursor + len;
	}
	joined[used] = '\0';
	*result = joined;
	return 0;
}

int sword3_paths_mkdirs(const char *path, mode_t mode)
{
	char *normalized;
	char *cursor;
	struct stat status;
	int saved_errno;

	if (normalize_absolute(path, &normalized) < 0)
		return -1;

	for (cursor = normalized + 1; ; cursor++) {
		if (*cursor != '/' && *cursor != '\0')
			continue;
		if (*cursor == '/') {
			*cursor = '\0';
			if (mkdir(normalized, mode) < 0) {
				if (errno != EEXIST ||
				    stat(normalized, &status) < 0 ||
				    !S_ISDIR(status.st_mode))
					goto fail;
			}
			*cursor = '/';
			continue;
		}
		if (mkdir(normalized, mode) < 0) {
			if (errno != EEXIST ||
			    stat(normalized, &status) < 0 ||
			    !S_ISDIR(status.st_mode)) {
				if (errno == EEXIST)
					errno = ENOTDIR;
				goto fail;
			}
		}
		break;
	}

	free(normalized);
	return 0;

fail:
	saved_errno = errno;
	free(normalized);
	errno = saved_errno;
	return -1;
}

static int make_data_root(char **result)
{
	const char *configured = getenv("SWORD3_DATA_DIR");
	const char *xdg;
	const char *home;
	char *candidate;
	size_t length;
	int rc;

	if (configured && *configured)
		return normalize_absolute(configured, result);

	xdg = getenv("XDG_DATA_HOME");
	if (xdg && *xdg) {
		if (xdg[0] != '/') {
			errno = EINVAL;
			return -1;
		}
		length = strlen(xdg);
		if (length > SIZE_MAX - sizeof("/sword3")) {
			errno = ENAMETOOLONG;
			return -1;
		}
		candidate = malloc(length + sizeof("/sword3"));
		if (!candidate) {
			errno = ENOMEM;
			return -1;
		}
		memcpy(candidate, xdg, length);
		memcpy(candidate + length, "/sword3", sizeof("/sword3"));
		rc = normalize_absolute(candidate, result);
		free(candidate);
		return rc;
	}

	home = getenv("HOME");
	if (!home || home[0] != '/') {
		errno = ENOENT;
		return -1;
	}
	length = strlen(home);
	if (length > SIZE_MAX - sizeof("/.local/share/sword3")) {
		errno = ENAMETOOLONG;
		return -1;
	}
	candidate = malloc(length + sizeof("/.local/share/sword3"));
	if (!candidate) {
		errno = ENOMEM;
		return -1;
	}
	memcpy(candidate, home, length);
	memcpy(candidate + length, "/.local/share/sword3",
	       sizeof("/.local/share/sword3"));
	rc = normalize_absolute(candidate, result);
	free(candidate);
	return rc;
}

int sword3_paths_init(Sword3Paths *paths)
{
	const char *bundle;
	Sword3Paths initialized = {0};
	int data_fd = -1;

	if (!paths) {
		errno = EINVAL;
		return -1;
	}
	bundle = getenv("SWORD3_BUNDLE_DIR");
	if (!bundle || !*bundle) {
		errno = ENOENT;
		return -1;
	}
	if (normalize_absolute(bundle, &initialized.bundle_dir) < 0)
		return -1;
	if (make_data_root(&initialized.data_dir) < 0)
		goto fail;
	if (sword3_paths_mkdirs(initialized.data_dir, 0755) < 0)
		goto fail;
	data_fd = open(initialized.data_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (data_fd < 0)
		goto fail;
	/*
	 * Reserve zero as the uninitialized value so destroying a zeroed
	 * Sword3Paths cannot accidentally close standard input.
	 */
	initialized.data_dir_fd = fcntl(data_fd, F_DUPFD_CLOEXEC, 3);
	if (initialized.data_dir_fd < 0)
		goto fail;
	close(data_fd);
	data_fd = -1;

	*paths = initialized;
	return 0;

fail:
	{
		int saved_errno = errno;
		if (data_fd >= 0)
			close(data_fd);
		if (initialized.data_dir_fd > 0)
			close(initialized.data_dir_fd);
		free(initialized.bundle_dir);
		free(initialized.data_dir);
		errno = saved_errno;
	}
	return -1;
}

void sword3_paths_destroy(Sword3Paths *paths)
{
	if (!paths)
		return;
	if (paths->data_dir_fd > 0)
		close(paths->data_dir_fd);
	free(paths->bundle_dir);
	free(paths->data_dir);
	paths->bundle_dir = NULL;
	paths->data_dir = NULL;
	paths->data_dir_fd = 0;
}

int sword3_paths_resource(const Sword3Paths *paths, const char *relative,
			  char **result)
{
	if (!paths || !paths->bundle_dir) {
		errno = EINVAL;
		return -1;
	}
	return join_relative(paths->bundle_dir, relative, result);
}

int sword3_paths_data(const Sword3Paths *paths, const char *relative,
		      char **result)
{
	if (!paths || !paths->data_dir) {
		errno = EINVAL;
		return -1;
	}
	return join_relative(paths->data_dir, relative, result);
}

static int write_all(int fd, const unsigned char *data, size_t size)
{
	while (size) {
		ssize_t written = write(fd, data, size);

		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (written == 0) {
			errno = EIO;
			return -1;
		}
		data += (size_t)written;
		size -= (size_t)written;
	}
	return 0;
}

static int open_data_parent(const Sword3Paths *paths, const char *relative,
			    int *parent_fd, char **leaf)
{
	char *copy = NULL;
	char *save = NULL;
	char *part;
	char *pending = NULL;
	int fd = -1;

	if (!paths || !relative || !parent_fd || !leaf ||
	    paths->data_dir_fd <= 0 || relative[0] == '/') {
		errno = EINVAL;
		return -1;
	}
	copy = strdup(relative);
	if (!copy) {
		errno = ENOMEM;
		return -1;
	}
	fd = fcntl(paths->data_dir_fd, F_DUPFD_CLOEXEC, 3);
	if (fd < 0)
		goto fail;

	part = strtok_r(copy, "/", &save);
	while (part) {
		if (!strcmp(part, "..")) {
			errno = EPERM;
			goto fail;
		}
		if (!strcmp(part, ".")) {
			part = strtok_r(NULL, "/", &save);
			continue;
		}
		if (pending) {
			if (mkdirat(fd, pending, 0755) < 0 && errno != EEXIST)
				goto fail;
			{
				int child = openat(fd, pending,
						 O_RDONLY | O_DIRECTORY |
					 O_CLOEXEC | O_NOFOLLOW);

				if (child < 0) {
					if (errno == ELOOP)
						errno = EPERM;
					goto fail;
				}
				close(fd);
				fd = child;
			}
		}
		pending = part;
		part = strtok_r(NULL, "/", &save);
	}
	if (pending) {
		*leaf = strdup(pending);
		if (!*leaf) {
			errno = ENOMEM;
			goto fail;
		}
		*parent_fd = fd;
		free(copy);
		return 0;
	}
	errno = EINVAL;

fail:
	{
		int saved_errno = errno;
		if (fd >= 0)
			close(fd);
		free(copy);
		errno = saved_errno;
	}
	return -1;
}

int sword3_paths_atomic_write(const Sword3Paths *paths, const char *relative,
			      const void *data, size_t size, mode_t mode)
{
	char *leaf = NULL;
	char temporary[96] = {0};
	int fd = -1;
	int parent_fd = -1;
	int saved_errno;
	int rc = -1;
	unsigned int attempt;

	if ((!data && size) ||
	    open_data_parent(paths, relative, &parent_fd, &leaf) < 0)
		return -1;

	for (attempt = 0; attempt < 128; attempt++) {
		int length = snprintf(temporary, sizeof(temporary),
				      ".sword3-tmp-%ld-%u",
				      (long)getpid(), attempt);

		if (length < 0 || (size_t)length >= sizeof(temporary)) {
			errno = ENAMETOOLONG;
			goto done;
		}
		fd = openat(parent_fd, temporary,
			    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
			    O_NOFOLLOW, 0600);
		if (fd >= 0)
			break;
		if (errno != EEXIST)
			goto done;
	}
	if (fd < 0) {
		errno = EEXIST;
		goto done;
	}
	if (fchmod(fd, mode) < 0 ||
	    write_all(fd, (const unsigned char *)data, size) < 0 ||
	    fsync(fd) < 0)
		goto done;
	if (close(fd) < 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (renameat(parent_fd, temporary, parent_fd, leaf) < 0)
		goto done;
	temporary[0] = '\0';

	if (fsync(parent_fd) < 0)
		goto done;
	rc = 0;

done:
	saved_errno = errno;
	if (fd >= 0)
		close(fd);
	if (rc < 0 && temporary[0])
		unlinkat(parent_fd, temporary, 0);
	if (parent_fd >= 0)
		close(parent_fd);
	free(leaf);
	if (rc < 0)
		errno = saved_errno;
	return rc;
}
