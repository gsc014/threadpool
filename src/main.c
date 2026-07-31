#include <threadpool.h>
#include <functions.h>
#include<unistd.h>

int main(){
    Threadpool_t *threadpool = init_threadpool();

    atomic_int counter = 0;

    Increment_counter_args_t args = {
        .counter = &counter,
        .amount = 5,
    };

    add_task(threadpool, increment_counter_task, &args);

    while(1){
        sleep(1);
        printf("Counter value: %d\n", atomic_load(&counter));
    }

    shutdown_threadpool(threadpool);
    return 0;
}
