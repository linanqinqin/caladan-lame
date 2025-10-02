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
#include <x86intrin.h>  // For RDTSC

#define NUM_THREADS_MAX 256
#define MIN_MATRIX_SIZE 1024
#define MAX_MATRIX_SIZE 2048

// Global variables
int enable_lame = 0;
int total_tasks = -1;  // -1 means infinite, otherwise total number of tasks to run
int tasks_completed = 0;  // Counter for completed tasks

// Enable per-task measurement mode (set by -m)
static int measure_mode = 0;

// Thread arguments structure
typedef struct {
    int thread_id;
    int *shared_counter;
    pthread_mutex_t *counter_mutex;
    int matrix_size;  // Each thread gets its own matrix size
    int *total_lames;  // Shared counter for total LAME interrupts
    unsigned long long *total_tsc_ticks;  // Shared counter for total TSC ticks
    pthread_mutex_t *stats_mutex;  // Mutex for statistics
    unsigned long long *total_duration_ns; // Sum of durations in ns (measure mode)
    int *measured_tasks; // Number of measured tasks
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
// Using worst possible memory access order (k-i-j) to maximize LLC cache misses
void matrix_multiply(int *A, int *B, int *C, int size, int *lame_count, unsigned long long *tsc_ticks) {
    // Initialize result matrix to zero
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            C[i * size + j] = 0;
        }
    }
    
    // Worst possible order: k-i-j (maximizes cache misses)
    for (int k = 0; k < size; k++) {
        for (int i = 0; i < size; i++) {
            for (int j = 0; j < size; j++) {
                // Use long long to prevent integer overflow
                long long temp = (long long)A[i * size + k] * B[k * size + j];
                C[i * size + j] += (int)(temp % 1000000); // Keep result manageable
            }
        }
        if (enable_lame) { // Only trigger LAME interrupt if enabled
            unsigned long long tsc_before = __rdtsc();
            __asm__ volatile("int $0x1f"); // trigger LAME interrupt
            unsigned long long tsc_after = __rdtsc();
            
            (*lame_count)++;
            (*tsc_ticks) += (tsc_after - tsc_before);
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
    
    // Determine matrix size (force MAX_MATRIX_SIZE in measure mode)
    if (measure_mode) {
        matrix_size = MAX_MATRIX_SIZE;
    }
    
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
    
    if (!measure_mode)
        printf("Thread %d: Starting %dx%d matrix multiplication...\n", thread_id, matrix_size, matrix_size);
    
    // Measurement
    struct timespec ts_start, ts_end;
    if (measure_mode)
        clock_gettime(CLOCK_MONOTONIC, &ts_start);
    
    // Perform matrix multiplication with LAME measurement
    int local_lame_count = 0;
    unsigned long long local_tsc_ticks = 0;
    matrix_multiply(A, B, C, matrix_size, &local_lame_count, &local_tsc_ticks);
    
    if (measure_mode) {
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        long long dur_ns = (long long)(ts_end.tv_sec - ts_start.tv_sec) * 1000000000LL +
                           (long long)(ts_end.tv_nsec - ts_start.tv_nsec);
        double dur_s = (double)dur_ns / 1e9;
        printf("Thread %d: MEASURE [size=%d] duration_ns=%lld (%.6f s) lames=%d tsc=%llu\n",
               thread_id, matrix_size, dur_ns, dur_s, local_lame_count, local_tsc_ticks);
        // aggregate
        pthread_mutex_lock(args->stats_mutex);
        if (args->total_duration_ns) *args->total_duration_ns += (unsigned long long)dur_ns;
        if (args->measured_tasks) (*args->measured_tasks)++;
        pthread_mutex_unlock(args->stats_mutex);
    }
    
    // Verify result (sum of all elements)
    long long result_sum = verify_matrix_multiply(A, B, C, matrix_size);
    if (!measure_mode)
        printf("Thread %d: Matrix multiplication completed. [thread_id=%d][size=%d][sum=%lld][lames=%d][tsc=%llu]\n", 
           thread_id, thread_id, matrix_size, result_sum, local_lame_count, local_tsc_ticks);
    
    // Clean up
    free(A);
    free(B);
    free(C);
    
    // Update shared counter with proper synchronization
    pthread_mutex_lock(args->counter_mutex);
    (*args->shared_counter)++;
    int current_count = *args->shared_counter;
    tasks_completed++;  // Update global task counter
    pthread_mutex_unlock(args->counter_mutex);
    
    // Update LAME statistics
    pthread_mutex_lock(args->stats_mutex);
    (*args->total_lames) += local_lame_count;
    (*args->total_tsc_ticks) += local_tsc_ticks;
    pthread_mutex_unlock(args->stats_mutex);
    
    printf("Thread %d finished. Total completed: %d\n", thread_id, current_count);
    
    return NULL;
}

int main(int argc, char *argv[])
{
    int num_threads;
    thread_args_t *thread_args;
    int shared_counter = 0;
    pthread_mutex_t counter_mutex;
    pthread_mutex_t stats_mutex;
    int total_lames = 0;
    unsigned long long total_tsc_ticks = 0;
    unsigned long long total_duration_ns = 0;
    int measured_tasks = 0;
    int i, ret;
    int thread_counter = 0;  // Global thread counter for unique IDs
    
    // Parse command line arguments using getopt
    int opt;
    num_threads = 4;  // Default value
    enable_lame = 0;  // Default value
    total_tasks = -1;  // Default to infinite
    
    while ((opt = getopt(argc, argv, "w:lt:m")) != -1) {
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
            case 't':
                total_tasks = atoi(optarg);
                if (total_tasks <= 0) {
                    printf("Error: Total tasks must be greater than 0\n");
                    return 1;
                }
                break;
            case 'm':
                measure_mode = 1;
                break;
            default:
                printf("Usage: %s [-w num_threads] [-l] [-t total_tasks] [-m]\n", argv[0]);
                printf("  -w num_threads: Number of worker threads (default: 4)\n");
                printf("  -l: Enable LAME interrupts (default: disabled)\n");
                printf("  -t total_tasks: Total number of tasks to run (default: infinite)\n");
                printf("  -m: Measure mode (per-task timing; suppress intra-task prints; use MAX_MATRIX_SIZE)\n");
                printf("  Program runs continuously, spawning new threads as old ones finish\n");
                printf("Example: %s -w 8 -l -t 100 -m\n", argv[0]);
                return 1;
        }
    }
    
    // Seed random number generator for matrix sizes
    srand(time(NULL));
    
    printf("Hello, World from Caladan with POSIX threading!\n");
    printf("Spawning %d worker threads with random matrix sizes (%d-%d)...\n", 
           num_threads, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE);
    printf("LAME interrupts via INT: %s\n", enable_lame ? "ENABLED" : "DISABLED");
    if (total_tasks > 0) {
        printf("Total tasks to run: %d\n", total_tasks);
    } else {
        printf("Running continuously - press Ctrl+C to stop\n");
    }
    
    if (measure_mode) {
        printf("\nMeasure mode enabled. Press Enter to start measurements...\n");
        fflush(stdout);
        int c;
        // Consume until newline
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    
    // Allocate array for thread arguments
    thread_args = malloc(num_threads * sizeof(thread_args_t));
    
    if (!thread_args) {
        printf("Failed to allocate memory for thread arguments\n");
        return 1;
    }
    
    // Initialize mutexes
    if (pthread_mutex_init(&counter_mutex, NULL) != 0) {
        printf("Failed to initialize counter mutex\n");
        free(thread_args);
        return 1;
    }
    
    if (pthread_mutex_init(&stats_mutex, NULL) != 0) {
        printf("Failed to initialize stats mutex\n");
        pthread_mutex_destroy(&counter_mutex);
        free(thread_args);
        return 1;
    }
    
    
    // Main continuous loop
    while (1) {
        // Check if we've reached the total task limit
        if (total_tasks > 0 && thread_counter >= total_tasks) {
            printf("Reached total task assignment (%d). Waiting for remaining threads to complete...\n", total_tasks);
            
            // Wait for all remaining threads to complete
            while (shared_counter < thread_counter) {
                usleep(100000);  // 100ms
            }
            
            printf("All tasks completed successfully!\n");
            printf("Final statistics: %d threads spawned, %d tasks completed\n", thread_counter, tasks_completed);
            
            // Print LAME statistics
            if (enable_lame && total_lames > 0) {
                printf("\n=== LAME Performance Statistics ===\n");
                printf("Total LAME interrupts: %d\n", total_lames);
                printf("Total TSC ticks for LAME overhead: %llu\n", total_tsc_ticks);
                printf("Average TSC ticks per LAME: %.2f\n", (double)total_tsc_ticks / total_lames);
                printf("LAME overhead percentage: %.4f%%\n", 
                       (double)total_tsc_ticks / (total_tsc_ticks + (unsigned long long)thread_counter * 1000000) * 100);
            } else if (enable_lame) {
                printf("\nLAME was enabled but no interrupts were triggered.\n");
            }
            if (measure_mode && measured_tasks > 0) {
                double total_s = (double)total_duration_ns / 1e9;
                double avg_s = total_s / (double)measured_tasks;
                printf("\n=== Measure Mode Summary ===\n");
                printf("Measured tasks: %d\n", measured_tasks);
                printf("Total duration: %.6f s\n", total_s);
                printf("Average duration per task: %.6f s\n", avg_s);
            }
            break;
        }
        
        // Calculate how many threads are currently running
        int threads_running = thread_counter - shared_counter;
        int threads_to_spawn = num_threads - threads_running;
        
        // Don't spawn more threads if we're approaching the total task limit
        if (total_tasks > 0) {
            int remaining_to_assign = total_tasks - thread_counter;
            if (remaining_to_assign < 0) remaining_to_assign = 0;
            if (threads_to_spawn > remaining_to_assign) {
                threads_to_spawn = remaining_to_assign;
            }
        }
        
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
            thread_args[i].total_lames = &total_lames;
            thread_args[i].total_tsc_ticks = &total_tsc_ticks;
            thread_args[i].stats_mutex = &stats_mutex;
            thread_args[i].total_duration_ns = &total_duration_ns;
            thread_args[i].measured_tasks = &measured_tasks;
            
            // Determine matrix size
            if (measure_mode) {
                thread_args[i].matrix_size = MAX_MATRIX_SIZE;
            } else {
                thread_args[i].matrix_size = MIN_MATRIX_SIZE + (rand() % (MAX_MATRIX_SIZE - MIN_MATRIX_SIZE + 1));
            }
            if (!measure_mode)
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
    pthread_mutex_destroy(&stats_mutex);
    free(thread_args);
    
    return 0;
} 