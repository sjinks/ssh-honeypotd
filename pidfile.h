#ifndef PIDFILE_H_
#define PIDFILE_H_

int create_pid_file(const char* path);
int write_pid(int fd);

#endif /* PIDFILE_H_ */
