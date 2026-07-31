#include "headerfiles/functions.h"

#include <stdio.h>

void print_message_task(void *arguments){
    Print_message_args_t *print_args = arguments;
    if(print_args == NULL || print_args->message == NULL){
        return;
    }

    puts(print_args->message);
}

void increment_counter_task(void *arguments){
    Increment_counter_args_t *counter_args = arguments;
    if(counter_args == NULL || counter_args->counter == NULL){
        return;
    }

    atomic_fetch_add(counter_args->counter, counter_args->amount);
}

void sum_integers_task(void *arguments){
    Sum_integers_args_t *sum_args = arguments;
    if(sum_args == NULL || sum_args->values == NULL ||
       sum_args->result == NULL){
        return;
    }

    long long sum = 0;
    for(size_t i = 0; i < sum_args->value_count; ++i){
        sum += sum_args->values[i];
    }

    *sum_args->result = sum;
}
