#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stdbool.h>
#include <stdatomic.h>

#include "array.h"

#define THREADCOUNT 4

typedef struct Threadpool Threadpool_t;

typedef struct{
    int id;
    Worker_states_t state;
    Threadpool_t *threadpool;
} Worker_t;

typedef struct{
    int id;
    Task_states_t state; // does a task need a state? it will either be in the readyqeueue or in the waiting queue. there is no other states needed i think.
    void (*task)(void *);
    void *args;
}Task_t;

struct Threadpool{
    atomic_bool shutdown; // atomic bool that indicates if the threadpool is shutting down. If it is, then all threads will finish their current task and then exit.
    Array_t *main_thread_taskqueue; // queue of tasks that are ready to be executed by the main thread. some functions only want to be executed by the main thread, so they will be placed in this queue.
    queue_sensitive_t *global_taskqueue; // queue of tasks that are ready to be executed.
    Sensitive_array_t *waiting_queue; // queue of tasks that are waiting on a condition to be met.
    Array_t threads_array; // array of all spawned threads.
};

int free_threadpool();
void scheduler();
void add_task(Threadpool_t *threadpool, void (*task)(void *), void *args, bool main_thread_only);
Threadpool_t *init_threadpool();

#endif
