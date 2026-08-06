#include "./windows.h"

#include <errno.h>
#include <limits.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct{
    Platform_thread_fn function;
    void *argument;
} Windows_thread_start_t;

#ifndef NDEBUG
static atomic_uintptr_t next_thread_token = 1;
static _Thread_local uintptr_t current_thread_token;

static uintptr_t get_current_thread_token(void){
    if(current_thread_token == 0){
        current_thread_token = atomic_fetch_add(&next_thread_token, 1);
    }
    return current_thread_token;
}
#endif

static int windows_error(void){
    DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? EINVAL : (int)error;
}

static unsigned int __stdcall windows_thread_start(void *raw_start){
    Windows_thread_start_t *start = raw_start;
    Platform_thread_fn function = start->function;
    void *argument = start->argument;

    free(start);
    function(argument);
    return 0;
}

int platform_thread_create(
    Platform_thread_t *thread,
    Platform_thread_fn function,
    void *argument
){
    if(thread == NULL || function == NULL || thread->handle != NULL){
        return EINVAL;
    }

    Windows_thread_start_t *start = malloc(sizeof(*start));
    if(start == NULL){
        return ENOMEM;
    }

    start->function = function;
    start->argument = argument;

    uintptr_t handle = _beginthreadex(
        NULL,
        0,
        windows_thread_start,
        start,
        0,
        &thread->id
    );
    if(handle == 0){
        int result = errno == 0 ? EINVAL : errno;
        free(start);
        return result;
    }

    thread->handle = (HANDLE)handle;
    return 0;
}

int platform_thread_join(Platform_thread_t *thread){
    if(thread == NULL || thread->handle == NULL){
        return EINVAL;
    }

    DWORD result = WaitForSingleObject(thread->handle, INFINITE);
    if(result != WAIT_OBJECT_0){
        return windows_error();
    }

    if(!CloseHandle(thread->handle)){
        return windows_error();
    }

    thread->handle = NULL;
    thread->id = 0;
    return 0;
}

void platform_thread_yield(void){
    SwitchToThread();
}

int platform_mutex_init(Platform_mutex_t *mutex){
    if(mutex == NULL || mutex->initialized){
        return EINVAL;
    }

    InitializeSRWLock(&mutex->native);
#ifndef NDEBUG
    atomic_init(&mutex->owner_token, 0);
#endif
    mutex->initialized = true;
    return 0;
}

int platform_mutex_destroy(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
#ifndef NDEBUG
    if(atomic_load(&mutex->owner_token) != 0){
        return EBUSY;
    }
#endif

    mutex->initialized = false;
    return 0;
}

int platform_mutex_lock(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }

    AcquireSRWLockExclusive(&mutex->native);
#ifndef NDEBUG
    atomic_store(&mutex->owner_token, get_current_thread_token());
#endif
    return 0;
}

int platform_mutex_unlock(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
#ifndef NDEBUG
    if(!platform_mutex_is_owned_by_current_thread(mutex)){
        return EPERM;
    }

    atomic_store(&mutex->owner_token, 0);
#endif
    ReleaseSRWLockExclusive(&mutex->native);
    return 0;
}

int platform_mutex_trylock(Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return EINVAL;
    }

    if(TryAcquireSRWLockExclusive(&mutex->native)){
#ifndef NDEBUG
        atomic_store(&mutex->owner_token, get_current_thread_token());
#endif
        return 0;
    }else{
        return EBUSY;
    }
}

#ifndef NDEBUG
bool platform_mutex_is_owned_by_current_thread(const Platform_mutex_t *mutex){
    if(mutex == NULL || !mutex->initialized){
        return false;
    }
    return atomic_load(&mutex->owner_token) == get_current_thread_token();
}
#endif

int platform_condition_init(Platform_condition_t *condition){
    if(condition == NULL || condition->initialized){
        return EINVAL;
    }

    InitializeConditionVariable(&condition->native);
    condition->initialized = true;
    return 0;
}

int platform_condition_destroy(Platform_condition_t *condition){
    if(condition == NULL || !condition->initialized){
        return EINVAL;
    }

    condition->initialized = false;
    return 0;
}

int platform_condition_wait(
    Platform_condition_t *condition,
    Platform_mutex_t *mutex
){
    if(condition == NULL || !condition->initialized ||
       mutex == NULL || !mutex->initialized){
        return EINVAL;
    }
#ifndef NDEBUG
    if(!platform_mutex_is_owned_by_current_thread(mutex)){
        return EPERM;
    }

    atomic_store(&mutex->owner_token, 0);
#endif
    BOOL result = SleepConditionVariableSRW(
        &condition->native,
        &mutex->native,
        INFINITE,
        0
    );
#ifndef NDEBUG
    atomic_store(&mutex->owner_token, get_current_thread_token());
#endif
    if(!result){
        return windows_error();
    }
    return 0;
}

int platform_condition_signal(Platform_condition_t *condition){
    if(condition == NULL || !condition->initialized){
        return EINVAL;
    }

    WakeConditionVariable(&condition->native);
    return 0;
}

int platform_condition_broadcast(Platform_condition_t *condition){
    if(condition == NULL || !condition->initialized){
        return EINVAL;
    }

    WakeAllConditionVariable(&condition->native);
    return 0;
}

int platform_semaphore_init(
    Platform_semaphore_t *semaphore,
    unsigned int initial_count
){
    if(semaphore == NULL || semaphore->handle != NULL ||
       initial_count > (unsigned int)LONG_MAX){
        return EINVAL;
    }

    semaphore->handle = CreateSemaphoreA(
        NULL,
        (LONG)initial_count,
        LONG_MAX,
        NULL
    );
    return semaphore->handle == NULL ? windows_error() : 0;
}

int platform_semaphore_destroy(Platform_semaphore_t *semaphore){
    if(semaphore == NULL || semaphore->handle == NULL){
        return EINVAL;
    }

    if(!CloseHandle(semaphore->handle)){
        return windows_error();
    }

    semaphore->handle = NULL;
    return 0;
}

int platform_semaphore_wait(Platform_semaphore_t *semaphore){
    if(semaphore == NULL || semaphore->handle == NULL){
        return EINVAL;
    }

    DWORD result = WaitForSingleObject(semaphore->handle, INFINITE);
    return result == WAIT_OBJECT_0 ? 0 : windows_error();
}

int platform_semaphore_post(Platform_semaphore_t *semaphore){
    if(semaphore == NULL || semaphore->handle == NULL){
        return EINVAL;
    }

    return ReleaseSemaphore(semaphore->handle, 1, NULL)
        ? 0
        : windows_error();
}

unsigned int platform_hardware_thread_count(void){
    DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return count == 0 ? 1U : (unsigned int)count;
}

uint64_t platform_monotonic_nanoseconds(void){
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;

    if(!QueryPerformanceCounter(&counter) ||
       !QueryPerformanceFrequency(&frequency) ||
       frequency.QuadPart <= 0){
        return 0;
    }

    uint64_t ticks = (uint64_t)counter.QuadPart;
    uint64_t ticks_per_second = (uint64_t)frequency.QuadPart;

    return (ticks / ticks_per_second) * UINT64_C(1000000000) +
           ((ticks % ticks_per_second) * UINT64_C(1000000000)) /
               ticks_per_second;
}
