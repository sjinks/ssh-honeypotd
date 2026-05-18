#ifndef PIDFILE_H_
#define PIDFILE_H_

#include <sys/types.h>

int create_pid_file(const char* path, uid_t expected_uid);
int write_pid(int fd);

#endif /* PIDFILE_H_ */
