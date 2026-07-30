#ifndef ARRAY_H
#define ARRAY_H

#include <stddef.h>

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

void init_array();
void enqueue(queue_sensitive_t *queue, void *element);
void pop(queue_sensitive_t *queue, void *element);

#endif
