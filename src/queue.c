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
bool enlarge_queue(Queue_t *queue){
    if(queue == NULL || queue->elements == NULL || queue->capacity == 0){
        printf("Invalid parameter for enlarge_queue.\n");
        return false;
    }

    size_t new_capacity = queue->capacity + queue->capacity / 5;
    if(new_capacity == queue->capacity){
        new_capacity++;
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

    if(queue->active_count == queue->capacity){
        if(enlarge_queue(queue) == false){
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

bool dequeue(Queue_t *queue, void *element){
    if(queue->active_count == 0){
        return false;
    }

    memcpy(element,
           (char *)queue->elements + (queue->tail * queue->element_size),
           queue->element_size);
    
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->active_count--;
    return true;
}

bool move_queue(Queue_t *source, Queue_t *destination, size_t count){
    if(source == NULL || destination == NULL || source == destination ||
       source->elements == NULL || destination->elements == NULL ||
       source->element_size != destination->element_size || count == 0){
        return false;
    }

    if(count > source->active_count){
        count = source->active_count;
    }

    size_t free_slots = destination->capacity - destination->active_count;
    if(count > free_slots){
        count = free_slots;
    }

    if(count == 0){
        return false;
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
    return true;
}

int init_dual_queue(Dual_queue_t *dual_queue, size_t element_size, size_t capacity){
    if(dual_queue == NULL || element_size == 0 || capacity == 0){
        printf("Invalid parameters for init_dual_queue.\n");
        return -1;
    }

    if(init_queue(&dual_queue->queues[0], element_size, capacity) != 0){
        return -1;
    }
    if(init_queue(&dual_queue->queues[1], element_size, capacity) != 0){
        free_queue(&dual_queue->queues[0]);
        return -1;
    }
    atomic_init(&dual_queue->active_queue_index, false);

    return 0;
}

void free_dual_queue(Dual_queue_t *dual_queue){
    if(dual_queue == NULL){
        return;
    }

    free_queue(&dual_queue->queues[0]);
    free_queue(&dual_queue->queues[1]);
}

void enqueue_dual(Dual_queue_t *dual_queue, const void *element){
    bool active_index = atomic_load(&dual_queue->active_queue_index);
    Queue_t *active_queue = &dual_queue->queues[active_index];

    platform_mutex_lock(&active_queue->lock);
    enqueue(active_queue, element);
    platform_mutex_unlock(&active_queue->lock);

    return;
}

// will put a an element from the active queue into the provided pointer.
void dequeue_dual(Dual_queue_t *dual_queue, void *element){
    bool active_index = atomic_load(&dual_queue->active_queue_index);
    Queue_t *active_queue = &dual_queue->queues[active_index];

    // platform_mutex_lock(&active_queue->lock);
    bool result = dequeue(active_queue, element);
    // platform_mutex_unlock(&active_queue->lock);

    return;
}

void switch_active_queue(Dual_queue_t *dual_queue){
    atomic_fetch_xor(&dual_queue->active_queue_index, true);
    return;
}

// will move count elements from the source queue to the destination dual queue. if to_active_queue is true, it will move to the active queue, otherwise it will move to the buffer queue.
void move_queue_single_to_dual(Queue_t *source, Dual_queue_t *destination, size_t count, bool to_active_queue){
    bool active_index = atomic_load(&destination->active_queue_index);
    bool index = 0;
    if(to_active_queue){
        index = active_index;
    }
    else{
        index = !active_index;
    }
    Queue_t *target_queue = &destination->queues[index];

    platform_mutex_lock(&source->lock);
    platform_mutex_lock(&target_queue->lock);

    move_queue(source, target_queue, count);

    platform_mutex_unlock(&target_queue->lock);
    platform_mutex_unlock(&source->lock);
    return;
}

// assumes source and destination is locked
void move_queue_dual_to_single(Dual_queue_t *source, Queue_t *destination, size_t count, bool from_active_queue){
    bool active_index = atomic_load(&source->active_queue_index);
    bool index = 0;
    if(from_active_queue){
        index = active_index;
    }
    else{
        index = !active_index;
    }
    Queue_t *source_queue = &source->queues[index];

    // platform_mutex_lock(&source_queue->lock);
    // platform_mutex_lock(&destination->lock);

    move_queue(source_queue, destination, count);

    if(source_queue->active_count == 0){
        switch_active_queue(source);
    }

    // platform_mutex_unlock(&destination->lock);
    // platform_mutex_unlock(&source_queue->lock);
    return;
}