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
#include <sys/mman.h>
#include <pthread.h>
#include <mqueue.h>
#include <semaphore.h>

#define MESSAGE_QUEUE_REQUEST "/request_queue"
#define MESSAGE_QUEUE_RESPONSE "/response_queue"
#define SEM_MUTEX_NAME "/sem-mutex"
#define BUFSIZE (630)
#define MAX_MESSAGE_SIZE 2048

typedef struct shm_data_struct{
    size_t *segsize;
    char *name;
} shm_data_struct;

typedef struct message_queue_args {
    pthread_mutex_t mqueue_mutex;
    pthread_cond_t mqueue_cond;
    char *server;
} message_queue_args;

typedef struct job_args {
    steque_t *job_queue;
    pthread_mutex_t job_m;
    pthread_cond_t job_c;
} job_args;

typedef struct cache_req_args {
    char shm_name[10];
    char request_path[BUFSIZE];
    size_t segsize;
} cache_req_args;

typedef struct cache_res_args {
    gfstatus_t status;
    size_t size;
} cache_res_args;

typedef struct testReq {
	char data[BUFSIZE];
    size_t size;
} testReq;

extern steque_t *m_queue;
extern sem_t *mutex_sem;

#endif // __CACHE_STUDENT_H__