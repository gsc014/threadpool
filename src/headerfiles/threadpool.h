#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "array.h"
#include "platform_specific/api.h"


#define THREADCOUNT 4
#define TASKQUEUE_SIZE 256
#define WAITINGQUEUE_SIZE 64

typedef struct Threadpool Threadpool_t;

typedef struct{
    int id;
    Threadpool_t *threadpool;
    Platform_thread_t thread;
} Worker_t;

struct Threadpool{
    atomic_bool shutdown; // atomic bool that indicates if the threadpool is shutting down. If it is, then all threads will finish their current task and then exit.
    // Queue_t main_thread_taskqueue; // queue of tasks that are ready to be executed by the main thread. some functions only want to be executed by the main thread, so they will be placed in this queue.
    // Platform_mutex_t work_mutex; // mutex for locking
    // Platform_condition_t work_available; // condition variable

    Queue_sensitive_t global_taskqueue; // queue of tasks that are ready to be executed.
    Sensitive_array_t waiting_array; // queue of tasks that are waiting on a condition to be met.
    Worker_t *workers;
    size_t worker_count;
};

int free_threadpool(Threadpool_t *threadpool);
void scheduler(Worker_t *worker);
bool add_task(Threadpool_t *threadpool, void (*task)(void *), void *args);
void shutdown_threadpool(Threadpool_t *threadpool);
Threadpool_t *init_threadpool(void);

#endif
