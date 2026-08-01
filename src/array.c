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

// moves count tasks from source to destination.
bool move_queue(Queue_sensitive_t *source, Queue_sensitive_t *destination, size_t count){
    // given count is too high, so we will move as many tasks as possible.
    if (count > source->task_count) {
        count = source->task_count;
    }
    
    size_t free_slots = destination->array_info.capacity - destination->task_count;
    if (count > free_slots) {
        count = free_slots;
    }

    for(size_t i = 0; i < count; i++){
        Task_t task;
        bool found = pop(source, &task);
        if(!found){
            return false;
        }
        bool added = enqueue(destination, task);
        if(!added){
            return false;
        }
    }
    return true;
}