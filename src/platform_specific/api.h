#ifndef PLATFORM_API_H
#define PLATFORM_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <semaphore.h>
#else
#error "Only Linux and Windows are currently supported."
#endif

#ifndef NDEBUG
#define assert_mutex_owned(mutex) \
    assert(platform_mutex_is_owned_by_current_thread((mutex)))
#else
#define assert_mutex_owned(mutex) ((void)0)
#endif

typedef void (*Platform_thread_fn)(void *argument);

#if defined(_WIN32)

typedef struct{
    HANDLE handle;
    unsigned int id;
} Platform_thread_t;

typedef struct{
    SRWLOCK native;
    atomic_uintptr_t owner_token;
    bool initialized;
} Platform_mutex_t;

typedef struct{
    CONDITION_VARIABLE native;
    bool initialized;
} Platform_condition_t;

typedef struct{
    HANDLE handle;
} Platform_semaphore_t;

#elif defined(__linux__)

typedef struct{
    pthread_t native;
    bool joinable;
} Platform_thread_t;

typedef struct{
    pthread_mutex_t native;
    atomic_uintptr_t owner_token;
    bool initialized;
} Platform_mutex_t;

typedef struct{
    pthread_cond_t native;
    bool initialized;
} Platform_condition_t;

typedef struct{
    sem_t native;
    bool initialized;
} Platform_semaphore_t;

#endif

/*
 * All functions returning int use zero for success and non-zero for failure.
 * Platform objects must be zero-initialized before their init/create function.
 * A created thread must be joined exactly once.
 */
int platform_thread_create(
    Platform_thread_t *thread,
    Platform_thread_fn function,
    void *argument
);
int platform_thread_join(Platform_thread_t *thread);
void platform_thread_yield(void);

int platform_mutex_init(Platform_mutex_t *mutex);
int platform_mutex_destroy(Platform_mutex_t *mutex);
int platform_mutex_lock(Platform_mutex_t *mutex);
int platform_mutex_unlock(Platform_mutex_t *mutex);
int platform_mutex_trylock(Platform_mutex_t *mutex);
#ifndef NDEBUG
bool platform_mutex_is_owned_by_current_thread(const Platform_mutex_t *mutex);
#endif

int platform_condition_init(Platform_condition_t *condition);
int platform_condition_destroy(Platform_condition_t *condition);

/*
 * The mutex must be locked before calling wait. The function atomically
 * unlocks it while sleeping and locks it again before returning.
 * Always check the application predicate in a loop because wakeups may be
 * spurious.
 */
int platform_condition_wait(
    Platform_condition_t *condition,
    Platform_mutex_t *mutex
);
int platform_condition_signal(Platform_condition_t *condition);
int platform_condition_broadcast(Platform_condition_t *condition);

int platform_semaphore_init(
    Platform_semaphore_t *semaphore,
    unsigned int initial_count
);
int platform_semaphore_destroy(Platform_semaphore_t *semaphore);
int platform_semaphore_wait(Platform_semaphore_t *semaphore);
int platform_semaphore_post(Platform_semaphore_t *semaphore);

unsigned int platform_hardware_thread_count(void);
uint64_t platform_monotonic_nanoseconds(void);

#endif
