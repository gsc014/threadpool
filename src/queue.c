#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "headerfiles/queue.h"

int init_queue(Queue_t *queue, size_t element_size, size_t capacity){
    if(queue == NULL || element_size == 0 || capacity == 0){
        printf("Invalid parameters for init_queue.\n");
        return -1;
    }
    queue->capacity = capacity;
    queue->element_size = element_size;
    if(platform_mutex_init(&queue->lock) != 0){
        printf("Failed to initialize mutex for queue.\n");
        queue->capacity = 0;
        queue->element_size = 0;
        return -1;
    }
    queue->elements = calloc(capacity, element_size);
    if(queue->elements == NULL){
        printf("Failed to allocate memory for queue.\n");
        platform_mutex_destroy(&queue->lock);
        queue->capacity = 0;
        queue->element_size = 0;
        return -1;
    }
    queue->active_count = 0;
    queue->head = 0;
    queue->tail = 0;
    return 0;
}

void free_queue(Queue_t *queue){
    if(queue == NULL){
        return;
    }

    if(queue->lock.initialized){
        platform_mutex_destroy(&queue->lock);
    }

    free(queue->elements);
    queue->elements = NULL;
    queue->capacity = 0;
    queue->element_size = 0;
    queue->active_count = 0;
    queue->head = 0;
    queue->tail = 0;
    return;
}

// will enlarge the queue by approximately 20 percent.
bool enlarge_queue(Queue_t *queue, size_t new_capacity){
    
    if(queue == NULL || queue->elements == NULL || queue->capacity == 0){
        printf("Invalid parameter for enlarge_queue.\n");
        return false;
    }
    assert_mutex_owned(&queue->lock);

    if(new_capacity <= queue->capacity){
        new_capacity = queue->capacity + queue->capacity / 5;
        if(new_capacity == queue->capacity){
            new_capacity++;
        }
    }

    void *new_elements = malloc(new_capacity * queue->element_size);
    if(new_elements == NULL){
        printf("Failed to allocate memory for enlarged queue.\n");
        return false;
    }

    for(size_t index = 0; index < queue->active_count; index++){
        size_t old_index = (queue->tail + index) % queue->capacity;
        memcpy((char *)new_elements + index * queue->element_size,
               (char *)queue->elements + old_index * queue->element_size,
               queue->element_size);
    }

    free(queue->elements);
    queue->elements = new_elements;
    queue->capacity = new_capacity;
    queue->head = queue->active_count;
    queue->tail = 0;
    return true;
}

bool enqueue(Queue_t *queue, const void *element){
    if(queue == NULL || element == NULL || queue->elements == NULL){
        printf("Invalid parameters for enqueue.\n");
        return false;
    }

    assert_mutex_owned(&queue->lock);

    if(queue->active_count == queue->capacity){
        size_t new_capacity = queue->capacity + queue->capacity / 5;
        if(enlarge_queue(queue, new_capacity) == false){
            return false;
        }
    }

    memcpy((char *)queue->elements + (queue->head * queue->element_size),
           element,
           queue->element_size);

    queue->head = (queue->head + 1) % queue->capacity;
    queue->active_count++;
    return true;
}

bool dequeue(Queue_t *queue, void *element, bool from_back){
    if(queue == NULL || element == NULL || queue->elements == NULL){
        return false;
    }

    assert_mutex_owned(&queue->lock);

    if(queue->active_count == 0){
        return false;
    }

    if(from_back){
        size_t back_index = (queue->head + queue->capacity - 1) % queue->capacity;
        memcpy(element,
               (char *)queue->elements + back_index * queue->element_size,
               queue->element_size);
        queue->head = back_index;
    } else {
        memcpy(element,
               (char *)queue->elements + (queue->tail * queue->element_size),
               queue->element_size);
        queue->tail = (queue->tail + 1) % queue->capacity;
    }
    queue->active_count--;
    return true;
}

// returns how many elements were moved
size_t move_queue(Queue_t *source, Queue_t *destination, size_t count){
    if(source == NULL || destination == NULL || source == destination ||
       source->elements == NULL || destination->elements == NULL ||
       source->element_size != destination->element_size || count == 0){
        return 0;
    }

    assert_mutex_owned(&source->lock);
    assert_mutex_owned(&destination->lock);

    if(count > source->active_count){
        count = source->active_count;
    }

    if(count == 0){
        return 0;
    }

    if(count > SIZE_MAX - destination->active_count){
        return 0;
    }

    size_t required_capacity = destination->active_count + count;
    if(required_capacity > destination->capacity){
        if(!enlarge_queue(destination, required_capacity)){
            return 0;
        }
    }

    size_t source_index = source->tail;
    size_t destination_index = destination->head;
    size_t remaining = count;

    while(remaining > 0){
        size_t source_section_size = source->capacity - source_index;
        size_t destination_section_size = destination->capacity - destination_index;

        size_t copy_count = remaining;

        if(copy_count > source_section_size){
            copy_count = source_section_size;
        }
        if(copy_count > destination_section_size){
            copy_count = destination_section_size;
        }

        memcpy((char *)destination->elements + destination_index * destination->element_size,
            (char *)source->elements + source_index * source->element_size,
            copy_count * source->element_size);

        remaining -= copy_count;

        source_index = (source_index + copy_count) % source->capacity;
        destination_index = (destination_index + copy_count) % destination->capacity;
    }

    source->tail = source_index;
    source->active_count -= count;

    destination->head = destination_index;
    destination->active_count += count;
    return count;
}

int init_dual_queue(Dual_queue_t *dual_queue, size_t element_size, size_t capacity){
    if(dual_queue == NULL || element_size == 0 || capacity == 0){
        printf("Invalid parameters for init_dual_queue.\n");
        return -1;
    }

    atomic_init(&dual_queue->active_queue_index, false);
    atomic_init(&dual_queue->queued_task_count, 0);
    if(init_queue(&dual_queue->queues[0], element_size, capacity) != 0){
        return -1;
    }
    if(init_queue(&dual_queue->queues[1], element_size, capacity) != 0){
        free_queue(&dual_queue->queues[0]);
        return -1;
    }
    if (platform_mutex_init(&dual_queue->switch_lock) != 0) {
        free_queue(&dual_queue->queues[1]);
        free_queue(&dual_queue->queues[0]);
        return -1;
    }

    return 0;
}

void free_dual_queue(Dual_queue_t *dual_queue){
    if(dual_queue == NULL){
        return;
    }
    platform_mutex_destroy(&dual_queue->switch_lock);
    free_queue(&dual_queue->queues[0]);
    free_queue(&dual_queue->queues[1]);
}

// will manage to lock active queue sometimes by luck.
Queue_t *mutex_try_lock_active_queue(Dual_queue_t *dual_queue){
    bool active_index = atomic_load(&dual_queue->active_queue_index);
    if(platform_mutex_trylock(&dual_queue->queues[active_index].lock) == 0){
        if(active_index == atomic_load(&dual_queue->active_queue_index)){
            return &dual_queue->queues[active_index];
        }
        platform_mutex_unlock(&dual_queue->queues[active_index].lock);
    }
    return NULL;
}

Queue_t *mutex_lock_dual_queue(Dual_queue_t *dual, bool want_active)
{
    while(1) {
        bool active = atomic_load(&dual->active_queue_index);
        bool index = want_active ? active : !active;

        platform_mutex_lock(&dual->queues[index].lock);

        bool current_active =
            atomic_load(&dual->active_queue_index);
        bool expected =
            want_active ? current_active : !current_active;

        if (index == expected) {
            return &dual->queues[index];
        }

        platform_mutex_unlock(&dual->queues[index].lock);
    }
}
