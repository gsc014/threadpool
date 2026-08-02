#include <threadpool.h>
#include <functions.h>
#include <stdio.h>

int main(void){
    // size_t hardware_thread_count = platform_hardware_thread_count();
    size_t hardware_thread_count = 4;
    printf("Hardware thread count: %zu\n", hardware_thread_count);

    Threadpool_t *threadpool = init_threadpool(hardware_thread_count);
    if(threadpool == NULL){
        fprintf(stderr, "Failed to initialize thread pool.\n");
        return 1;
    }

    atomic_int counter = 0;

    Increment_counter_args_t args = {
        .counter = &counter,
        .amount = 5,
    };

    if(!add_task(threadpool, increment_counter_task, &args)){
        fprintf(stderr, "Failed to submit task.\n");
        shutdown_threadpool(threadpool);
        return 1;
    }

    while(atomic_load(&counter) != args.amount){
        platform_thread_yield();
    }

    printf("Counter value: %d\n", atomic_load(&counter));
    shutdown_threadpool(threadpool);
    return 0;
}
