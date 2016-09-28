#ifndef __BARRIER_H
#define __BARRIER_H

#include <stdint.h>
#include <pthread.h>

// a simple barrier implementation
// used to make sure all threads start the experiment at the same time
typedef struct barrier {
    pthread_cond_t complete;
    pthread_mutex_t mutex;
    int count;
    int crossing;
} barrier_t;

void barrier_init(barrier_t *b, int n);
void barrier_cross(barrier_t *b);

#endif
