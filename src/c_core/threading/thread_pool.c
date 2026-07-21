#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── Internal structures ─────────────────────────────────── */

typedef struct {
    ca_task_t   tasks[CA_POOL_QUEUE_SIZE]; // Circular task queue
    uint32_t    head;                      // Dequeue position
    uint32_t    tail;                      // Enqueue position
    uint32_t    count;                     // Tasks in queue
} ca_task_queue_t;

struct ca_thread_pool {
    pthread_t         threads[CA_POOL_MAX_THREADS];
    uint32_t          num_threads;

    ca_task_queue_t   queue;

    pthread_mutex_t   mutex;
    pthread_cond_t    task_available;
    pthread_cond_t    all_done;

    uint32_t          active_count;    // Workers currently executing
    uint64_t          completed_count; // Total tasks done
    int               shutdown;        // Signal workers to stop
    int               initialized;        
};

/* ── Worker thread function ──────────────────────────────── */
static void* worker_fn(void* arg) {
    ca_thread_pool_t* pool = (ca_thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        /* Wait until there's work or shutdown signal */
        while (pool->queue.count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->task_available, &pool->mutex);

        /* Exit if shutting down and no work left */
        if (pool->shutdown && pool->queue.count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Dequeue task */
        ca_task_t task    = pool->queue.tasks[pool->queue.head];
        pool->queue.head  = (pool->queue.head + 1) % CA_POOL_QUEUE_SIZE;
        pool->queue.count--;
        pool->active_count++;

        pthread_mutex_unlock(&pool->mutex);

        /* Execute task outside of lock */
        task.fn(task.arg);

        pthread_mutex_lock(&pool->mutex);
        pool->active_count--;
        pool->completed_count++;

        /* Signal if everything is done */
        if (pool->queue.count == 0 && pool->active_count == 0)
            pthread_cond_broadcast(&pool->all_done);

        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}

/* ── Create pool ─────────────────────────────────────────── */
ca_thread_pool_t* ca_pool_create(uint32_t num_threads) {
    if (num_threads == 0 || num_threads > CA_POOL_MAX_THREADS)
        return NULL;

    ca_thread_pool_t* pool = 
        (ca_thread_pool_t*)calloc(1, sizeof(ca_thread_pool_t));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->shutdown    = 0;
    pool->initialized = 0; /* DO NOT SPAWN THREADS YET */

    pthread_mutex_init(&pool->mutex,         NULL);
    pthread_cond_init (&pool->task_available, NULL);
    pthread_cond_init (&pool->all_done,       NULL);

    printf("[CoreAgent] Thread pool initialized (lazy loading active)\n");
    return pool;
}

/* ── Destroy pool ────────────────────────────────────────── */
void ca_pool_destroy(ca_thread_pool_t* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->task_available);
    pthread_mutex_unlock(&pool->mutex);

    if (pool->initialized) {
        for (uint32_t i = 0; i < pool->num_threads; i++)
            pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->task_available);
    pthread_cond_destroy(&pool->all_done);

    free(pool);
}

/* ── Submit task ─────────────────────────────────────────── */
int ca_pool_submit(ca_thread_pool_t* pool, ca_task_fn fn, void* arg) {
    if (!pool || !fn) return -1;

    pthread_mutex_lock(&pool->mutex);
    /* Lazy Thread Spawning + Stack Size Reduction */
    if (!pool->initialized && !pool->shutdown) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 512 * 1024); /* Cut from 8MB to 512KB */

        for (uint32_t i = 0; i < pool->num_threads; i++) {
            if (pthread_create(&pool->threads[i], &attr, worker_fn, pool) != 0) {
                fprintf(stderr, "[CoreAgent] Failed to lazy-spawn thread %u\n", i);
            }
        }
        pthread_attr_destroy(&attr);
        pool->initialized = 1;
    }

    if (pool->queue.count >= CA_POOL_QUEUE_SIZE) {
        fprintf(stderr, "[CoreAgent] Task queue full!\n");
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    ca_task_t task = { .fn = fn, .arg = arg };
    pool->queue.tasks[pool->queue.tail] = task;
    pool->queue.tail  = (pool->queue.tail + 1) % CA_POOL_QUEUE_SIZE;
    pool->queue.count++;

    pthread_cond_signal(&pool->task_available);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

/* ── Wait for all tasks ──────────────────────────────────── */
void ca_pool_wait(ca_thread_pool_t* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    while (pool->queue.count > 0 || pool->active_count > 0)
        pthread_cond_wait(&pool->all_done, &pool->mutex);
    pthread_mutex_unlock(&pool->mutex);
}

/* ── Stats ───────────────────────────────────────────────── */
uint64_t ca_pool_tasks_completed(const ca_thread_pool_t* pool) {
    return pool ? pool->completed_count : 0;
}

uint32_t ca_pool_active_threads(const ca_thread_pool_t* pool) {
    return pool ? pool->active_count : 0;
}