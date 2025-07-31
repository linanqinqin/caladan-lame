/*
 * hello_threads.c - A hello world application demonstrating POSIX threading on Caladan
 * 
 * This example shows how to write a standard POSIX multi-threaded application
 * that runs transparently on Caladan using pthreads.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#define NUM_THREADS 4

// Thread arguments structure
typedef struct {
    int thread_id;
    int *shared_counter;
    pthread_mutex_t *counter_mutex;
} thread_args_t;

// Thread function
void *worker_thread(void *arg)
{
    thread_args_t *args = (thread_args_t *)arg;
    int thread_id = args->thread_id;
    
    printf("Hello from worker thread %d!\n", thread_id);
    
    // Simulate some work
    usleep(100000); // 100ms
    
    // Update shared counter with proper synchronization
    pthread_mutex_lock(args->counter_mutex);
    (*args->shared_counter)++;
    int current_count = *args->shared_counter;
    pthread_mutex_unlock(args->counter_mutex);
    
    printf("Thread %d finished. Total completed: %d\n", thread_id, current_count);
    
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t threads[NUM_THREADS];
    thread_args_t thread_args[NUM_THREADS];
    int shared_counter = 0;
    pthread_mutex_t counter_mutex;
    int i, ret;
    
    printf("Hello, World from Caladan with POSIX threading!\n");
    printf("Spawning %d worker threads...\n", NUM_THREADS);
    
    // Initialize mutex
    if (pthread_mutex_init(&counter_mutex, NULL) != 0) {
        printf("Failed to initialize mutex\n");
        return 1;
    }
    
    // Create threads
    for (i = 0; i < NUM_THREADS; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].shared_counter = &shared_counter;
        thread_args[i].counter_mutex = &counter_mutex;
        
        ret = pthread_create(&threads[i], NULL, worker_thread, &thread_args[i]);
        if (ret != 0) {
            printf("Failed to create thread %d\n", i);
            return 1;
        }
    }
    
    // Wait for all threads to complete
    for (i = 0; i < NUM_THREADS; i++) {
        ret = pthread_join(threads[i], NULL);
        if (ret != 0) {
            printf("Failed to join thread %d\n", i);
            return 1;
        }
    }
    
    printf("All %d threads completed successfully!\n", NUM_THREADS);
    printf("Final counter value: %d\n", shared_counter);
    
    // Clean up
    pthread_mutex_destroy(&counter_mutex);
    
    return 0;
} 