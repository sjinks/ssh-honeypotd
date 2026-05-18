#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <libgen.h>
#include "pidfile.h"

#define BUF_SIZE 64

/*
 * PID file security model:
 * - The PID path is configuration, but may be supplied from service files or
 *   command lines that should not be able to make a root-started daemon clobber
 *   arbitrary files.
 * - PID files are therefore restricted to trusted runtime directories
 *   (/run and /var/run), opened relative to a validated directory fd, and never
 *   through caller-controlled intermediate symlinks.
 * - Existing files are accepted only if they already look exactly like PID
 *   files: regular, single-link, and owner-read/write only.  This avoids
 *   truncating hard links, device nodes, symlinks, or permissive stale files.
 * - When root starts the daemon and it will later drop privileges, a root-owned
 *   stale PID file from an older version is accepted for restart compatibility,
 *   but ownership is repaired only after the PID-file lock is acquired.
 */

static int lock_file(int fd, short int type, short int whence, off_t start, off_t len)
{
	struct flock fl;

	fl.l_type   = type;
	fl.l_whence = whence;
	fl.l_start  = start;
	fl.l_len    = len;
	fl.l_pid    = 0;

#ifdef F_OFD_SETLK
	return fcntl(fd, F_OFD_SETLK, &fl);
#else
	return fcntl(fd, F_SETLK, &fl);
#endif
}

static int is_trusted_pid_directory(const char* dir)
{
	/* Keep PID files in runtime filesystems; reject /tmp and arbitrary paths. */
	return (
		   strcmp(dir, "/run") == 0
		|| strncmp(dir, "/run/", 5) == 0
		|| strcmp(dir, "/var/run") == 0
		|| strncmp(dir, "/var/run/", 9) == 0
	);
}

static int resolve_pid_path(const char* path, char* resolved_dir, size_t resolved_dir_size, char* pid_name, size_t pid_name_size)
{
	if (!resolved_dir_size || !pid_name_size) {
		errno = EINVAL;
		return -1;
	}

	size_t path_len = path ? strlen(path) : 0;
	if (!path_len || path[path_len - 1] == '/') {
		/* dirname()/basename() would silently normalize trailing slashes. */
		errno = EINVAL;
		return -1;
	}

	char* path_copy = strdup(path);
	char* path_copy2 = strdup(path);
	if (!path_copy || !path_copy2) {
		int e = errno ? errno : ENOMEM;
		free(path_copy);
		free(path_copy2);
		errno = e;
		return -1;
	}

	char* dir = dirname(path_copy);
	char* base = basename(path_copy2);
	if (!dir || !base || base[0] == 0 || strcmp(base, ".") == 0 || strcmp(base, "..") == 0 || strchr(base, '/')) {
		free(path_copy);
		free(path_copy2);
		errno = EINVAL;
		return -1;
	}

	if (!realpath(dir, resolved_dir)) {
		/* The directory must already exist; only the final PID file may be new. */
		int e = errno;
		free(path_copy);
		free(path_copy2);
		errno = e;
		return -1;
	}

	if (strlen(resolved_dir) + 1 > resolved_dir_size) {
		free(path_copy);
		free(path_copy2);
		errno = ENAMETOOLONG;
		return -1;
	}

	size_t pid_name_len = strlen(base);
	if (pid_name_len + 1 > pid_name_size) {
		free(path_copy);
		free(path_copy2);
		errno = ENAMETOOLONG;
		return -1;
	}

	memcpy(pid_name, base, pid_name_len + 1);

	free(path_copy);
	free(path_copy2);

	return 0;
}

static int validate_pid_dir(int dirfd, const char* resolved_dir, uid_t expected_uid)
{
	struct stat dir_st;
	if (fstat(dirfd, &dir_st) == -1) {
		return -1;
	}

	if (!S_ISDIR(dir_st.st_mode)) {
		errno = EPERM;
		return -1;
	}

	if (!is_trusted_pid_directory(resolved_dir) || (dir_st.st_uid != 0 && dir_st.st_uid != expected_uid)) {
		/* A runtime directory is trusted if root or the daemon user owns it. */
		errno = EPERM;
		return -1;
	}

	if ((dir_st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
		/* Sticky directories still allow PID-name precreation and DoS races. */
		errno = EPERM;
		return -1;
	}

	return 0;
}

static int pid_file_mode_is_restricted(mode_t mode)
{
	/* Include special bits: 0600 with setuid/setgid/sticky is not acceptable. */
	return (mode & (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)) == (S_IRUSR | S_IWUSR);
}

static int validate_pid_file_type(const struct stat* st)
{
	if (!S_ISREG(st->st_mode)) {
		errno = EINVAL;
		return -1;
	}

	if (st->st_nlink != 1) {
		/* A PID file must not be a hard link to some other file. */
		errno = EMLINK;
		return -1;
	}

	return 0;
}

static int validate_existing_pid_file(int fd, uid_t expected_uid, int* repair_owner)
{
	struct stat st;
	if (fstat(fd, &st) == -1) {
		return -1;
	}

	if (validate_pid_file_type(&st) == -1) {
		return -1;
	}

	if (!pid_file_mode_is_restricted(st.st_mode)) {
		/* Do not chmod existing unsafe files; reject them before truncation. */
		errno = EPERM;
		return -1;
	}

	*repair_owner = 0;
	if (st.st_uid == expected_uid) {
		return 0;
	}

	/* Permit stale PID files from older root-run versions; repair after locking. */
	if (geteuid() == 0 && st.st_uid == 0 && expected_uid != 0) {
		*repair_owner = 1;
		return 0;
	}

	errno = EPERM;
	return -1;
}

static int set_pid_owner(int fd, uid_t expected_uid)
{
	struct stat st;
	if (fstat(fd, &st) == -1) {
		return -1;
	}

	if (validate_pid_file_type(&st) == -1) {
		return -1;
	}

	if (st.st_uid == expected_uid) {
		return 0;
	}

	if (geteuid() == 0 && st.st_uid == 0 && expected_uid != 0) {
		if (fchown(fd, expected_uid, (gid_t)-1) == -1) {
			return -1;
		}

		return 0;
	}

	errno = EPERM;
	return -1;
}

static int validate_pid_file(int fd, uid_t expected_uid)
{
	struct stat st;

#ifndef O_CLOEXEC
	/* Keep PID file descriptor out of execve() children on platforms without O_CLOEXEC. */
	int fd_flags = fcntl(fd, F_GETFD);
	if (fd_flags == -1 || fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) == -1) {
		return -1;
	}
#endif

	if (fstat(fd, &st) == -1) {
		return -1;
	}

	if (validate_pid_file_type(&st) == -1) {
		return -1;
	}

	if (st.st_uid != expected_uid) {
		errno = EPERM;
		return -1;
	}

	return 0;
}

static int restrict_pid_file_mode(int fd)
{
	struct stat st;
	if (fstat(fd, &st) == -1) {
		return -1;
	}

	if (!pid_file_mode_is_restricted(st.st_mode) && fchmod(fd, S_IRUSR | S_IWUSR) == -1) {
		return -1;
	}

	return 0;
}

static int open_resolved_dir(const char* resolved_dir)
{
	/*
	 * Walk the already-resolved absolute path from / using openat().  O_NOFOLLOW
	 * on each component prevents a directory entry from being swapped for a
	 * symlink between realpath() and use.
	 */
	char path[PATH_MAX + 1];
	if (strlen(resolved_dir) + 1 > sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	memcpy(path, resolved_dir, strlen(resolved_dir) + 1);

	int dir_flags = O_RDONLY;
#ifdef O_DIRECTORY
	dir_flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
	dir_flags |= O_CLOEXEC;
#endif

	int dirfd = open("/", dir_flags);
	if (dirfd == -1) {
		return -1;
	}

	char* component = path;
	while (*component == '/') {
		++component;
	}

	while (*component) {
		char* slash = strchr(component, '/');
		if (slash) {
			*slash = 0;
		}

		int component_flags = dir_flags;
#ifdef O_NOFOLLOW
		component_flags |= O_NOFOLLOW;
#endif
		int nextfd = openat(dirfd, component, component_flags);
		int e = errno;
		close(dirfd);
		if (nextfd == -1) {
			errno = e;
			return -1;
		}

		dirfd = nextfd;
		if (!slash) {
			break;
		}

		component = slash + 1;
		while (*component == '/') {
			++component;
		}
	}

	return dirfd;
}

static int open_validated_pid_dir(const char* resolved_dir, uid_t expected_uid)
{
	int dirfd = open_resolved_dir(resolved_dir);
	if (dirfd == -1) {
		return -1;
	}

	if (validate_pid_dir(dirfd, resolved_dir, expected_uid) == -1) {
		int e = errno;
		close(dirfd);
		errno = e;
		return -1;
	}

	return dirfd;
}

static int open_pid_file_at(int dirfd, const char* pid_name, int* created)
{
	/* Create first to avoid accepting a preexisting object unless we can prove it is safe. */
	int open_flags = O_RDWR;
#ifdef O_NOFOLLOW
	open_flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif

	*created = 1;
	int fd = openat(dirfd, pid_name, open_flags | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (fd == -1 && errno == EEXIST) {
		*created = 0;
		fd = openat(dirfd, pid_name, open_flags);
	}

	return fd;
}

static int validate_open_pid_file(int fd, uid_t expected_uid, int created)
{
	/* Pre-lock validation is non-mutating; it only rejects obviously unsafe files. */
	int repair_owner;
	if (!created && validate_existing_pid_file(fd, expected_uid, &repair_owner) == -1) {
		return -1;
	}

	return 0;
}

static int prepare_locked_pid_file(int fd, uid_t expected_uid, int created)
{
	/*
	 * Repeat validation after locking because the daemon user may have changed an
	 * existing PID file between open() and the lock acquisition.  Mutating fixes
	 * (chown/chmod) are deliberately delayed until this point so a second daemon
	 * cannot alter the running instance's PID file before discovering the lock.
	 */
	int repair_owner = created;
	if (!created && validate_existing_pid_file(fd, expected_uid, &repair_owner) == -1) {
		return -1;
	}

	if (repair_owner && set_pid_owner(fd, expected_uid) == -1) {
		return -1;
	}

	if (validate_pid_file(fd, expected_uid) == -1) {
		return -1;
	}

	if (created && restrict_pid_file_mode(fd) == -1) {
		return -1;
	}

	return 0;
}

int create_pid_file(const char* path, uid_t expected_uid)
{
	char resolved_dir[PATH_MAX + 1];
	char pid_name[NAME_MAX + 1];
	int created = 0;
	if (resolve_pid_path(path, resolved_dir, sizeof(resolved_dir), pid_name, sizeof(pid_name)) == -1) {
		return -1;
	}

	int dirfd = open_validated_pid_dir(resolved_dir, expected_uid);
	if (dirfd == -1) {
		return -1;
	}

	int fd = open_pid_file_at(dirfd, pid_name, &created);
	int open_errno = errno;
	if (close(dirfd) == -1 && fd != -1) {
		close(fd);
		return -1;
	}

	if (-1 == fd) {
		errno = open_errno;
		return -1;
	}

	if (validate_open_pid_file(fd, expected_uid, created) == -1) {
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}

	if (lock_file(fd, F_WRLCK, SEEK_SET, 0, 0) == -1) {
		int e = errno;
		close(fd);
		errno = e;
		if (e == EAGAIN || e == EACCES) {
			/* PID file locked - another instance is running */
			return -2;
		}

		return -1;
	}

	if (prepare_locked_pid_file(fd, expected_uid, created) == -1) {
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}

	if (ftruncate(fd, 0) == -1) {
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}

#ifndef F_OFD_SETLK
	if (lock_file(fd, F_UNLCK, SEEK_SET, 0, 0) == -1) {
		assert(0);
	}
#endif

	return fd;
}

int write_pid(int fd)
{
	char buf[BUF_SIZE];
	snprintf(buf, BUF_SIZE, "%ld\n", (long int)getpid());

#ifndef F_OFD_SETLK
	if (lock_file(fd, F_WRLCK, SEEK_SET, 0, 0)) {
		return -1;
	}
#endif

	if (write(fd, buf, strlen(buf)) != (ssize_t)strlen(buf)) {
		return -1;
	}

	return fsync(fd);
}
