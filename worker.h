#ifndef __BOLT_WORKER_H
#define __BOLT_WORKER_H

int bolt_init_workers(int num);
int bolt_worker_pass_task(bolt_connection_t *c);

#endif
