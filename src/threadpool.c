#define THREADCOUNT 4

// Used to keep track of an array.
typedef struct{
    int count; // how many spaces are there in the array.
    size_t size; // what size are the elements?
    void *elements; // pointer to the elements. 

    int active_count; // how many elements are currently in the array.
} Array_t;

// used for arrays that cannot be altered by others. It is locked.
typedef struct{
    lock;
    Array_t array;
} Sensitive_array_t;

// elements are added to the head and removed from the tail. This is a circular array that is sensitive to the order of elements. It is locked.
typedef struct{
    lock;
    Array_t array;
    head;
    tail; // current position
} queue_sensitive_t;

// what is the condition? how can it be met?
typedef enum{
    IS_ZERO,
} Condition_states_t;

// When a using multiple threads, maybe multiple threads will be waiting on a condition to be met. This is a condition, it should be used when waiting on something. When the condition is met, the thread that made the condition will continue to run it or will place it in a ready queue.
typedef struct{
    lock;
    state; // not sure on this
    current value; // how will this even work? how can we store a condition and check it?
} Condition_t;

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

typedef struct{
    atomic_bool shutdown; // atomic bool that indicates if the threadpool is shutting down. If it is, then all threads will finish their current task and then exit.
    Array_t *main_thread_taskqueue; // queue of tasks that are ready to be executed by the main thread. some functions only want to be executed by the main thread, so they will be placed in this queue.
    queue_sensitive_t *global_taskqueue; // queue of tasks that are ready to be executed.
    Sensitive_array_t *waiting_queue; // queue of tasks that are waiting on a condition to be met.
    Array_t threads_array; // array of all spawned threads.
} Threadpool_t;

// frees threadpool and all structures within.
int free_threadpool(){
    /*free threadpool and all structures within, including threads*/
    return 1;
}

// this function is used by threads to find new tasks to execute.
void scheduler(){
    while(!threadpool->shutdown){
        /*the main thread will check its own task queue before checking the global queue.
        then we check global queue for tasks.

        */
    }
    while(/*will wait until all threads have completed their work and is waiting in a common area*/)
    if(threadpool.thread.id == 0){
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
    // malloc space for threadpool
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

// condition check, count down, is condition 0? how can we do that? should a condition have a state? then we can check based on the state, maybe the state is is_zero, or something like that. or should we call the function ourself? what is a good plan?
void condition_check(Condition_t *condition){
    // check if condition is met.
    lock condition;

    bool condition_met = false;
    switch(condition.state){
        case IS_ZERO:
            if(condition.current_value == 0){
                condition_met = true;
            }
            break;
        default:
            // this should never happen
    }
    // condition is met, move from waiting queue to ready queue. Or whatever we want to do. Do we want to try to switch to it?
}

// array spesific functions
void init_array(){

}

void enqueue(queue_sensitive_t *queue, void *element){
    // add element to head of queue
}

void pop(queue_sensitive_t *queue, void *element){
    // remove element from tail of queue
}

int main(){
    // init threadpool
    return 1;
}