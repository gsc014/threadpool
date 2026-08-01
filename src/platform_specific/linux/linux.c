#define _POSIX_C_SOURCE 200809L

#include "./linux.h"

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef struct{
    Platform_thread_fn function;
    void *argument;
} Linux_thread_start_t;

static void *linux_thread_start(void *raw_start){
    Linux_thread_start_t *start = raw_start;
    Platform_thread_fn function = start->function;
    void *argument = start->argument;

    free(start);
    function(argument);
    return NULL;
}

int platform_thread_create(
    Platform_thread_t *thread,
    Platform_thread_fn function,
    void *argument
){
    if(thread == NULL || function == NULL || thread->joinable){
        return EINVAL;
    }

    Linux_thread_start_t *start = malloc(sizeof(*start));
    if(start == NULL){
        return ENOMEM;
    }

    start->function = function;
    start->argument = argument;

    int result = pthread_create(
        &thread->native,
        NULL,
        linux_thread_start,
        start
    );
    if(result != 0){
        free(start);
        return result;
    }

    thread->joinable = true;
    return 0;
}

int platform_thread_join(Platform_thread_t *thread){
    if(thread == NULL || !thread->joinable){
        return EINVAL;
    }

    int result = pthread_join(thread->native, NULL);
    if(result == 0){
        thread->joinable = false;
    }
    return result;
}

void platform_thread_yield(void){
    sched_yield();
}

int platform_mutex_init(Platform_mutex_t *mutex){
    if(mutex == NULL || mutex->initialized){
        return EINVAL;
    }

    int result = pthread_mutex_init(&mutex->native, NULL);
    if(result == 0){
        mutex->initialized = true;
    }
    return result;
}

int platform_mutex_destroy(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }

    int result = pthread_mutex_destroy(&mutex->native);
    if(result == 0){
        mutex->initialized = false;
    }
    return result;
}

int platform_mutex_lock(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
    return pthread_mutex_lock(&mutex->native);
}

int platform_mutex_unlock(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
    return pthread_mutex_unlock(&mutex->native);
}

int platform_mutex_trylock(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
    return pthread_mutex_trylock(&mutex->native);
}

int platform_condition_init(Platform_condition_t *condition){
    if(condition == NULL || condition->initialized){
        return EINVAL;
    }

    int result = pthread_cond_init(&condition->native, NULL);
    if(result == 0){
        condition->initialized = true;
    }
    return result;
}

int platform_condition_destroy(Platform_condition_t *condition){
    if(condition == NULL || !condition->initialized){
        return EINVAL;
    }

    int result = pthread_cond_destroy(&condition->native);
    if(result == 0){
        condition->initialized = false;
    }
    return result;
}

int platform_condition_wait(
    Platform_condition_t *condition,
    Platform_mutex_t *mutex
){
    if(condition == NULL || !condition->initialized ||
       mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
    return pthread_cond_wait(&condition->native, &mutex->native);
}

int platform_condition_signal(Platform_condition_t *condition){
    if(condition == NULL || !condition->initialized){
        return EINVAL;
    }
    return pthread_cond_signal(&condition->native);
}

int platform_condition_broadcast(Platform_condition_t *condition){
    if(condition == NULL || !condition->initialized){
        return EINVAL;
    }
    return pthread_cond_broadcast(&condition->native);
}

int platform_semaphore_init(
    Platform_semaphore_t *semaphore,
    unsigned int initial_count
){
    if(semaphore == NULL || semaphore->initialized){
        return EINVAL;
    }

    if(sem_init(&semaphore->native, 0, initial_count) != 0){
        return errno;
    }

    semaphore->initialized = true;
    return 0;
}

int platform_semaphore_destroy(Platform_semaphore_t *semaphore){
    if(semaphore == NULL || !semaphore->initialized){
        return EINVAL;
    }

    if(sem_destroy(&semaphore->native) != 0){
        return errno;
    }

    semaphore->initialized = false;
    return 0;
}

int platform_semaphore_wait(Platform_semaphore_t *semaphore){
    if(semaphore == NULL || !semaphore->initialized){
        return EINVAL;
    }

    while(sem_wait(&semaphore->native) != 0){
        if(errno != EINTR){
            return errno;
        }
    }
    return 0;
}

int platform_semaphore_post(Platform_semaphore_t *semaphore){
    if(semaphore == NULL || !semaphore->initialized){
        return EINVAL;
    }

    if(sem_post(&semaphore->native) != 0){
        return errno;
    }
    return 0;
}

unsigned int platform_hardware_thread_count(void){
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (unsigned int)count : 1U;
}

uint64_t platform_monotonic_nanoseconds(void){
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time) != 0){
        return 0;
    }

    return (uint64_t)time.tv_sec * UINT64_C(1000000000) +
           (uint64_t)time.tv_nsec;
}
