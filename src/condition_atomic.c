#include "headerfiles/condition_atomic.h"

int update_atomic_condition(Condition_atomic_t *condition){
    if(condition == NULL || condition->task.task == NULL){
        return -1;
    }

    size_t remaining = atomic_load_explicit(&condition->remaining, memory_order_acquire);

    while(remaining != 0){
        if(atomic_compare_exchange_weak_explicit(&condition->remaining, &remaining, remaining - 1, memory_order_acq_rel, memory_order_acquire)){
            if(remaining == 1){
                Task_t task = condition->task;
                task.task(task.args);
                return 1;
            }
            return 0;
        }
    }
    return -1;
}

void init_atomic_condition(Condition_atomic_t *condition, size_t initial_value, const Task_t *task){
    if(condition == NULL || task == NULL || task->task == NULL ||
       initial_value == 0){
        return;
    }

    condition->task = *task;
    atomic_init(&condition->remaining, initial_value);
}

void reset_atomic_condition(Condition_atomic_t *condition, size_t new_value){
    if(condition == NULL){
        return;
    }

    atomic_store_explicit(&condition->remaining, new_value, memory_order_release);
}

void destroy_atomic_condition(Condition_atomic_t *condition){
    if(condition == NULL){
        return;
    }

    atomic_store_explicit(&condition->remaining, 0, memory_order_release);
    condition->task = (Task_t){0};
}
