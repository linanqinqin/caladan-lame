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
#include <time.h>

#define NUM_THREADS 4
#define MATRIX_SIZE 512  // Size of matrices for multiplication

// Thread arguments structure
typedef struct {
    int thread_id;
    int *shared_counter;
    pthread_mutex_t *counter_mutex;
} thread_args_t;

/* linanqinqin */
// Function to generate a random matrix
void generate_random_matrix(int *matrix, int size) {
    for (int i = 0; i < size * size; i++) {
        matrix[i] = rand() ; // Random values between 0 and 99
    }
}

// Function to perform matrix multiplication: C = A * B
void matrix_multiply(int *A, int *B, int *C, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            C[i * size + j] = 0;
            for (int k = 0; k < size; k++) {
                C[i * size + j] += A[i * size + k] * B[k * size + j];
            }
        }
    }
}

// Function to verify matrix multiplication result
long long verify_matrix_multiply(int *A, int *B, int *C, int size) {
    long long sum = 0;
    for (int i = 0; i < size * size; i++) {
        sum += C[i];
    }
    return sum;
}
/* end */

// Thread function
void *worker_thread(void *arg)
{
    thread_args_t *args = (thread_args_t *)arg;
    int thread_id = args->thread_id;
    
    printf("Hello from worker thread %d!\n", thread_id);
    
    /* linanqinqin */
    // Perform matrix multiplication computation
    int *A = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(int));
    int *B = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(int));
    int *C = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(int));
    
    if (!A || !B || !C) {
        printf("Thread %d: Failed to allocate memory for matrices\n", thread_id);
        free(A);
        free(B);
        free(C);
        return NULL;
    }
    
    // Generate random matrices
    generate_random_matrix(A, MATRIX_SIZE);
    generate_random_matrix(B, MATRIX_SIZE);
    
    printf("Thread %d: Starting %dx%d matrix multiplication...\n", thread_id, MATRIX_SIZE, MATRIX_SIZE);
    
    // Perform matrix multiplication
    matrix_multiply(A, B, C, MATRIX_SIZE);
    
    // Verify result (sum of all elements)
    long long result_sum = verify_matrix_multiply(A, B, C, MATRIX_SIZE);
    printf("Thread %d: Matrix multiplication completed. Result sum: %lld\n", thread_id, result_sum);
    
    // Clean up
    free(A);
    free(B);
    free(C);
    /* end */
    
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
    
    /* linanqinqin */
    // Seed random number generator
    srand(time(NULL));
    /* end */
    
    printf("Hello, World from Caladan with POSIX threading!\n");
    printf("Spawning %d worker threads for %dx%d matrix multiplication...\n", NUM_THREADS, MATRIX_SIZE, MATRIX_SIZE);
    
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