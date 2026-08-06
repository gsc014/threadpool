#include <threadpool.h>

#include <stdio.h>
#include <stdlib.h>

#include<unistd.h>

Condition_t condition;

void work(void *arg){
    unsigned int index = (unsigned int)(uintptr_t)arg;
    printf("Task %u is running\n", index);

    update_condition(&condition);
    return;
}

void done(){
    printf("All tasks completed!\n");
    return;
}

int main(void){
    size_t hardware_thread_count = platform_hardware_thread_count();
    size_t task_count = hardware_thread_count * 10;
    printf("Hardware thread count: %zu\n", hardware_thread_count);

    Threadpool_t *threadpool = init_threadpool(hardware_thread_count);
    if(threadpool == NULL){
        fprintf(stderr, "Failed to initialize thread pool.\n");
        return 1;
    }

    Queue_t task_queue = {0};
    if(init_queue(&task_queue, sizeof(Task_t), task_count) != 0){
        fprintf(stderr, "Failed to initialize task queue.\n");
        shutdown_threadpool(threadpool);
        return 1;
    }

    platform_mutex_lock(&task_queue.lock);
    for(size_t i = 0; i < task_count; i++){
        Task_t task = {.task = work, .args = (void *)(uintptr_t)i};
        enqueue(&task_queue, &task);
    }
    platform_mutex_unlock(&task_queue.lock);

    Task_t completion_task = {
        .task = done,
        .args = NULL,
    };
    init_condition(&condition, task_count, &completion_task);

    add_task_bulk(threadpool, &task_queue);

    sleep(1);

    shutdown_threadpool(threadpool);
    destroy_condition(&condition);
    free_queue(&task_queue);
    return 0;
}
