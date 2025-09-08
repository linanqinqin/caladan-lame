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
#include <getopt.h>

#define NUM_THREADS_MAX 256
#define MIN_MATRIX_SIZE 128
#define MAX_MATRIX_SIZE 512

// Global variable for LAME enable flag
int enable_lame = 0;

// Thread arguments structure
typedef struct {
    int thread_id;
    int *shared_counter;
    pthread_mutex_t *counter_mutex;
    int matrix_size;  // Each thread gets its own matrix size
} thread_args_t;

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

// Thread function
void *worker_thread(void *arg)
{
    thread_args_t *args = (thread_args_t *)arg;
    int thread_id = args->thread_id;
    int matrix_size = args->matrix_size;
    
    printf("Hello from worker thread %d!\n", thread_id);
    
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
    thread_args_t *thread_args;
    int shared_counter = 0;
    pthread_mutex_t counter_mutex;
    int i, ret;
    int thread_counter = 0;  // Global thread counter for unique IDs
    
    // Parse command line arguments using getopt
    int opt;
    num_threads = 4;  // Default value
    enable_lame = 0;  // Default value
    
    while ((opt = getopt(argc, argv, "w:l")) != -1) {
        switch (opt) {
            case 'w':
                num_threads = atoi(optarg);
                if (num_threads <= 0 || num_threads > NUM_THREADS_MAX) {
                    printf("Error: Number of threads must be between 1 and %d\n", NUM_THREADS_MAX);
                    return 1;
                }
                break;
            case 'l':
                enable_lame = 1;
                break;
            default:
                printf("Usage: %s [-w num_threads] [-l]\n", argv[0]);
                printf("  -w num_threads: Number of worker threads (default: 4)\n");
                printf("  -l: Enable LAME interrupts (default: disabled)\n");
                printf("  Program runs continuously, spawning new threads as old ones finish\n");
                printf("Example: %s -w 8 -l\n", argv[0]);
                return 1;
        }
    }
    
    // Seed random number generator for matrix sizes
    srand(time(NULL));
    
    printf("Hello, World from Caladan with POSIX threading!\n");
    printf("Spawning %d worker threads with random matrix sizes (%d-%d)...\n", 
           num_threads, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE);
    printf("LAME interrupts: %s\n", enable_lame ? "ENABLED" : "DISABLED");
    printf("Running continuously - press Ctrl+C to stop\n");
    
    // Allocate array for thread arguments
    thread_args = malloc(num_threads * sizeof(thread_args_t));
    
    if (!thread_args) {
        printf("Failed to allocate memory for thread arguments\n");
        return 1;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&counter_mutex, NULL) != 0) {
        printf("Failed to initialize mutex\n");
        free(thread_args);
        return 1;
    }
    
    
    // Main continuous loop
    while (1) {
        // Calculate how many threads are currently running
        int threads_running = thread_counter - shared_counter;
        int threads_to_spawn = num_threads - threads_running;
        
        if (threads_to_spawn > 0) {
            printf("Threads spawned: %d, completed: %d, running: %d, need to spawn: %d\n", 
                   thread_counter, shared_counter, threads_running, threads_to_spawn);
        }
        
        // Spawn only the number of threads needed to maintain num_threads active
        for (i = 0; i < threads_to_spawn; i++) {
            pthread_t temp_thread;
            thread_args[i].thread_id = thread_counter++;
            thread_args[i].shared_counter = &shared_counter;
            thread_args[i].counter_mutex = &counter_mutex;
            
            // Generate random matrix size for this thread
            thread_args[i].matrix_size = MIN_MATRIX_SIZE + (rand() % (MAX_MATRIX_SIZE - MIN_MATRIX_SIZE + 1));
            printf("Spawning thread %d with matrix size: %dx%d\n", 
                   thread_args[i].thread_id, thread_args[i].matrix_size, thread_args[i].matrix_size);
            
            ret = pthread_create(&temp_thread, NULL, worker_thread, &thread_args[i]);
            if (ret != 0) {
                printf("Failed to create thread %d\n", thread_args[i].thread_id);
            } else {
                // Detach the thread so it cleans up automatically when done
                pthread_detach(temp_thread);
            }
        }
        
        // Sleep to let threads run and complete
        usleep(1000);  // 1ms
    }
    
    // Clean up (this will never be reached due to infinite loop)
    pthread_mutex_destroy(&counter_mutex);
    free(thread_args);
    
    return 0;
} 