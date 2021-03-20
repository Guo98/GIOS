/*
*  This file is for use by students to define anything they wish.  It is used by the proxy cache implementation
*/
#ifndef __CACHE_STUDENT_H__
#define __CACHE_STUDENT_H__

#include "steque.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <pthread.h>

typedef struct shm_data_struct{
    size_t *segsize;
    char *name;
} shm_data_struct;

typedef struct message_queue_args {
    steque_t *message_queue;
    pthread_mutex_t *mqueue_mutex;
    pthread_cond_t *mqueue_cond;
} message_queue_args;


#endif // __CACHE_STUDENT_H__