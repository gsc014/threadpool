#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stddef.h>
#include <stdatomic.h>

typedef struct{
    const char *message;
} Print_message_args_t;

typedef struct{
    atomic_int *counter;
    int amount;
} Increment_counter_args_t;

typedef struct{
    const int *values;
    size_t value_count;
    long long *result;
} Sum_integers_args_t;

void print_message_task(void *arguments);
void increment_counter_task(void *arguments);
void sum_integers_task(void *arguments);

#endif
