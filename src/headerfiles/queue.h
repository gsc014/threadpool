#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#include "../platform_specific/api.h"

typedef struct{
    size_t capacity;
    size_t element_size;
    void *elements;

    size_t active_count;
    size_t tail;
    size_t head;

    Platform_mutex_t lock;
} Queue_t;

int init_queue(Queue_t *queue, size_t element_size, size_t capacity);
void free_queue(Queue_t *queue);

/* The caller must hold the relevant queue lock while using these operations. */
bool enlarge_queue(Queue_t *queue);
bool enqueue(Queue_t *queue, const void *element);
bool dequeue(Queue_t *queue, void *element);
bool move_queue(Queue_t *source, Queue_t *destination, size_t count);

#endif
