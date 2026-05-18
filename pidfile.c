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

static int lock_file(int fd, short int type, short int whence, off_t start, off_t len)
{
	struct flock fl;

	fl.l_type   = type;
	fl.l_whence = whence;
	fl.l_start  = start;
	fl.l_len    = len;
	fl.l_pid    = 0;

	return fcntl(fd, F_SETLK, &fl);
}

static int is_trusted_pid_directory(const char* dir)
{
	return (
		   strcmp(dir, "/run") == 0
		|| strncmp(dir, "/run/", 5) == 0
		|| strcmp(dir, "/var/run") == 0
		|| strncmp(dir, "/var/run/", 9) == 0
	);
}

static int validate_pid_path(const char* path)
{
	uid_t euid = geteuid();

	char* path_copy = strdup(path);
	if (!path_copy) {
		return -1;
	}

	char resolved[PATH_MAX + 1];
	char* dir = dirname(path_copy);
	if (!dir) {
		free(path_copy);
		errno = EINVAL;
		return -1;
	}

	if (!realpath(dir, resolved)) {
		int e = errno;
		free(path_copy);
		errno = e;
		return -1;
	}

	free(path_copy);

	struct stat dir_st;
	if (stat(resolved, &dir_st) == -1) {
		return -1;
	}

	if (!S_ISDIR(dir_st.st_mode)) {
		errno = EPERM;
		return -1;
	}

	if (euid == 0) {
		if (!is_trusted_pid_directory(resolved) || dir_st.st_uid != 0) {
			errno = EPERM;
			return -1;
		}
	}
	else if (dir_st.st_uid != euid && (dir_st.st_mode & S_ISVTX) == 0) {
		errno = EPERM;
		return -1;
	}

	if (((dir_st.st_mode & S_IWOTH) != 0 && (dir_st.st_mode & S_ISVTX) == 0) || ((dir_st.st_mode & S_IWGRP) != 0 && (dir_st.st_mode & S_ISVTX) == 0)) {
		errno = EPERM;
		return -1;
	}

	return 0;
}

static int validate_pid_file(int fd)
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

	if (!S_ISREG(st.st_mode)) {
		errno = EINVAL;
		return -1;
	}

	if (st.st_uid != geteuid()) {
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

	if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0 && fchmod(fd, S_IRUSR | S_IWUSR) == -1) {
		return -1;
	}

	return 0;
}

int create_pid_file(const char* path)
{
	if (validate_pid_path(path) == -1) {
		return -1;
	}

	int open_flags = O_RDWR | O_CREAT | O_NOFOLLOW;
#ifdef O_CLOEXEC
	open_flags |= O_CLOEXEC;
#endif

	int fd = open(path, open_flags, S_IRUSR | S_IWUSR);
	if (-1 == fd) {
		return -1;
	}

	if (validate_pid_file(fd) == -1) {
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

	if (restrict_pid_file_mode(fd) == -1) {
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
