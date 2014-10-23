#ifndef WORKER_H_
#define WORKER_H_

#include "globals.h"

void* worker(void* arg);
void finalize_connection(struct connection_info_t* conn);

#endif /* WORKER_H_ */
