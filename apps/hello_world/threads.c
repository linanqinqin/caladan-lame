/*
 * threads.c - A hello world application demonstrating POSIX threading on Caladan
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

#define NUM_THREADS_MAX 256
#define MIN_MATRIX_SIZE 128
#define MAX_MATRIX_SIZE 512

// Thread arguments structure
typedef struct {
    int thread_id;
    int *shared_counter;
    pthread_mutex_t *counter_mutex;
    int matrix_size;  // Each thread gets its own matrix size
    int enable_lame;  // Whether to enable LAME interrupts
} thread_args_t;

/* linanqinqin */
// Function to generate a deterministic matrix A based on i,j indices
void generate_matrix_A(int *matrix, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            matrix[i * size + j] = (i + j) % 100; // A[i,j] = (i + j) % 100
        }
    }
}

// Function to generate a deterministic matrix B based on i,j indices
void generate_matrix_B(int *matrix, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            matrix[i * size + j] = (i * j + 1) % 100; // B[i,j] = (i * j + 1) % 100
        }
    }
}

// Function to perform matrix multiplication: C = A * B
void matrix_multiply(int *A, int *B, int *C, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            C[i * size + j] = 0;
            for (int k = 0; k < size; k++) {
                // Use long long to prevent integer overflow
                long long temp = (long long)A[i * size + k] * B[k * size + j];
                C[i * size + j] += (int)(temp % 1000000); // Keep result manageable
            }
        }
        if (enable_lame) { // Only trigger LAME interrupt if enabled
            __asm__ volatile("int $0x1f"); // trigger LAME interrupt
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
    int matrix_size = args->matrix_size;
    
    printf("Hello from worker thread %d!\n", thread_id);
    
    /* linanqinqin */
    // Perform matrix multiplication computation
    int *A = malloc(matrix_size * matrix_size * sizeof(int));
    int *B = malloc(matrix_size * matrix_size * sizeof(int));
    int *C = malloc(matrix_size * matrix_size * sizeof(int));
    
    if (!A || !B || !C) {
        printf("Thread %d: Failed to allocate memory for %dx%d matrices\n", thread_id, matrix_size, matrix_size);
        free(A);
        free(B);
        free(C);
        return NULL;
    }
    
    // Generate deterministic matrices based on size
    generate_matrix_A(A, matrix_size);
    generate_matrix_B(B, matrix_size);
    
    printf("Thread %d: Starting %dx%d matrix multiplication...\n", thread_id, matrix_size, matrix_size);
    
    // Perform matrix multiplication
    matrix_multiply(A, B, C, matrix_size);
    
    // Verify result (sum of all elements)
    long long result_sum = verify_matrix_multiply(A, B, C, matrix_size);
    printf("Thread %d: Matrix multiplication completed. [thread_id=%d][size=%d][sum=%lld]\n", 
           thread_id, thread_id, matrix_size, result_sum);
    
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
    int num_threads;
    int enable_lame;
    pthread_t *threads;
    thread_args_t *thread_args;
    int shared_counter = 0;
    pthread_mutex_t counter_mutex;
    int i, ret;
    
    /* linanqinqin */
    // Check command line arguments
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <number_of_threads> [enable_lame]\n", argv[0]);
        printf("Example: %s 4\n", argv[0]);
        printf("Example: %s 4 1\n", argv[0]);
        printf("  Second argument (any value) enables LAME interrupts\n");
        return 1;
    }
    
    num_threads = atoi(argv[1]);
    enable_lame = (argc == 3);  // Enable LAME if second argument is present
    
    if (num_threads <= 0 || num_threads > NUM_THREADS_MAX) {
        printf("Error: Number of threads must be between 1 and %d\n", NUM_THREADS_MAX);
        return 1;
    }
    
    // Seed random number generator for matrix sizes
    srand(time(NULL));
    /* end */
    
    printf("Hello, World from Caladan with POSIX threading!\n");
    printf("Spawning %d worker threads with random matrix sizes (%d-%d)...\n", 
           num_threads, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE);
    printf("LAME interrupts: %s\n", enable_lame ? "ENABLED" : "DISABLED");
    
    /* linanqinqin */
    // Allocate arrays for threads and arguments
    threads = malloc(num_threads * sizeof(pthread_t));
    thread_args = malloc(num_threads * sizeof(thread_args_t));
    
    if (!threads || !thread_args) {
        printf("Failed to allocate memory for threads\n");
        free(threads);
        free(thread_args);
        return 1;
    }
    /* end */
    
    // Initialize mutex
    if (pthread_mutex_init(&counter_mutex, NULL) != 0) {
        printf("Failed to initialize mutex\n");
        free(threads);
        free(thread_args);
        return 1;
    }
    
    // Create threads
    for (i = 0; i < num_threads; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].shared_counter = &shared_counter;
        thread_args[i].counter_mutex = &counter_mutex;
        /* linanqinqin */
        // Generate random matrix size for this thread
        thread_args[i].matrix_size = MIN_MATRIX_SIZE + (rand() % (MAX_MATRIX_SIZE - MIN_MATRIX_SIZE + 1));
        thread_args[i].enable_lame = enable_lame;  // Pass LAME enable flag to thread
        printf("Thread %d will use matrix size: %dx%d\n", i, thread_args[i].matrix_size, thread_args[i].matrix_size);
        /* end */
        
        ret = pthread_create(&threads[i], NULL, worker_thread, &thread_args[i]);
        if (ret != 0) {
            printf("Failed to create thread %d\n", i);
            pthread_mutex_destroy(&counter_mutex);
            free(threads);
            free(thread_args);
            return 1;
        }
    }
    
    // Wait for all threads to complete
    for (i = 0; i < num_threads; i++) {
        ret = pthread_join(threads[i], NULL);
        if (ret != 0) {
            printf("Failed to join thread %d\n", i);
            pthread_mutex_destroy(&counter_mutex);
            free(threads);
            free(thread_args);
            return 1;
        }
    }
    
    printf("All %d threads completed successfully!\n", num_threads);
    printf("Final counter value: %d\n", shared_counter);
    
    // Clean up
    pthread_mutex_destroy(&counter_mutex);
    free(threads);
    free(thread_args);
    
    return 0;
} 