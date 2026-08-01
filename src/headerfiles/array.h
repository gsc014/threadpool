#ifndef ARRAY_H
#define ARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>
#include <math.h>

#include "../platform_specific/api.h"

typedef struct{
    int id;
    void (*task)(void *);
    void *args;
} Task_t;

// Describes task storage owned and allocated by the caller.
typedef struct{
    size_t capacity;
    Task_t *elements;
} Array_t;

// used for arrays that cannot be altered by others. It is locked.
typedef struct{
    atomic_flag lock;
    Array_t array_info;
} Sensitive_array_t;

// elements are added to the head and removed from the tail. This is a circular array that is sensitive to the order of elements. It is locked.
typedef struct{
    Platform_mutex_t lock; // mutex for locking

    Array_t array_info; // holds all info about the array.
    size_t head;
    size_t tail; // current position

    size_t task_count; // number of occupied queue slots
} Queue_sensitive_t;

typedef struct{
    Array_t array_info; // holds all info about the array.
    size_t head;
    size_t tail; // current position
} Queue_t;

/*
 * The caller must hold queue->lock while calling these functions.
 * enqueue returns false when the queue is full; pop returns false when empty.
 */
bool enqueue(Queue_sensitive_t *queue, Task_t task);
bool pop(Queue_sensitive_t *queue, Task_t *task);
bool move_queue(Queue_sensitive_t *source, Queue_sensitive_t *destination, size_t count);

#endif
