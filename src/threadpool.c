#include "headerfiles/threadpool.h"

// frees threadpool and all structures within.
int free_threadpool(){
    /*free threadpool and all structures within, including threads*/
    return 1;
}

// this function is used by threads to find new tasks to execute.
void scheduler(Threadpool_t *threadpool){
    while(!threadpool->shutdown){
        /*the main thread will check its own task queue before checking the global queue.
        then we check global queue for tasks.

        */
    }
    while(/*will wait until all threads have completed their work and is waiting in a common area*/)
    if(threadpool->thread.id == 0){
        free_threadpool();
    }
    return;
}

// adds a task to threadpool
void add_task(Threadpool_t *threadpool, void (*task)(void *), void *args, bool main_thread_only = false /*false by default*/){
    // is this a task that needs to be executed by the main thread? If so, add it to the main thread task queue.
    // else add it to the global task queue.
}

// inits the threadpool.
Threadpool_t *init_threadpool(){
    Threadpool_t *threadpool = malloc(sizeof(Threadpool_t)); // allocate space for threadpool itself
    // malloc space for main thread task queue
    
    // malloc space for global task queue
    // malloc space for thread array
    for(int i = 0; i < THREADCOUNT; i++){
        // spawn thread;
        // insert into thread array;
        // send all threads to scheduler function.
    }
    return threadpool;
}
