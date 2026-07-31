#include "headerfiles/array.h"

bool enqueue(Queue_sensitive_t *queue, Task_t task){
    if(queue == NULL || task.task == NULL ||
       queue->array_info.elements == NULL ||
       queue->array_info.capacity == 0 ||
       queue->task_count >= queue->array_info.capacity){
        return false;
    }

    queue->array_info.elements[queue->head] = task;
    queue->head = (queue->head + 1) % queue->array_info.capacity;
    ++queue->task_count;

    return true;
}

bool pop(Queue_sensitive_t *queue, Task_t *task){
    if(queue == NULL || task == NULL ||
       queue->array_info.elements == NULL ||
       queue->array_info.capacity == 0 ||
       queue->task_count == 0){
        return false;
    }

    *task = queue->array_info.elements[queue->tail];
    queue->tail = (queue->tail + 1) % queue->array_info.capacity;
    --queue->task_count;

    return true;
}
