/*
 * mm.c - Matrix multiplication benchmark with parallel computation
 * 
 * Takes -g (matrix size 2^g x 2^g), -n (number of trials), and optional -v (verifier)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <getopt.h>
#include <stdbool.h>

// Thread arguments structure
typedef struct {
    int thread_id;
    int num_threads;
    unsigned long *A;
    unsigned long *B;
    unsigned long *C;
    int size;
    int start_row;
    int end_row;
} thread_args_t;

// Global variables
static int g_size = 0;
static int g_num_trials = 0;
static bool g_verify = false;
static unsigned long *g_A = NULL;
static unsigned long *g_B = NULL;
static unsigned long g_ground_truth_sum = 0;

// Get current time in seconds
static double get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

// Generate matrix A: deterministic pattern based on indices and g
static void generate_matrix_A(unsigned long *A, int size, int g) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            // Deterministic but appears random: use hash-like function
            unsigned long val = (unsigned long)(i * 2654435761UL + j * 2246822519UL + g * 3266489917UL);
            A[i * size + j] = val;
        }
    }
}

// Generate matrix B: different deterministic pattern
static void generate_matrix_B(unsigned long *B, int size, int g) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            // Different pattern for B
            unsigned long val = (unsigned long)(i * 2246822519UL + j * 3266489917UL + g * 2654435761UL);
            B[i * size + j] = val;
        }
    }
}

// Worker thread function for parallel matrix multiplication
static void *worker_thread(void *arg) {
    thread_args_t *args = (thread_args_t *)arg;
    unsigned long *A = args->A;
    unsigned long *B = args->B;
    unsigned long *C = args->C;
    int size = args->size;
    
    // Process assigned rows
    for (int i = args->start_row; i < args->end_row; i++) {
        for (int j = 0; j < size; j++) {
            unsigned long sum = 0;
            for (int k = 0; k < size; k++) {
                sum += A[i * size + k] * B[k * size + j];
            }
            // Guard by modulo 100
            C[i * size + j] = sum % 100;
        }
    }
    
    return NULL;
}

// Parallel matrix multiplication
static void parallel_multiply(unsigned long *A, unsigned long *B, unsigned long *C, 
                              int size, int num_threads) {
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    thread_args_t *args = malloc(num_threads * sizeof(thread_args_t));
    
    if (!threads || !args) {
        fprintf(stderr, "Failed to allocate memory for threads\n");
        exit(1);
    }
    
    // Calculate rows per thread
    int rows_per_thread = size / num_threads;
    int remainder = size % num_threads;
    
    // Create threads
    int start_row = 0;
    for (int i = 0; i < num_threads; i++) {
        args[i].thread_id = i;
        args[i].num_threads = num_threads;
        args[i].A = A;
        args[i].B = B;
        args[i].C = C;
        args[i].size = size;
        args[i].start_row = start_row;
        
        // Distribute remainder rows
        int rows = rows_per_thread + (i < remainder ? 1 : 0);
        args[i].end_row = start_row + rows;
        start_row = args[i].end_row;
        
        if (pthread_create(&threads[i], NULL, worker_thread, &args[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            exit(1);
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    free(args);
}

// Single-threaded matrix multiplication for verification
static void single_thread_multiply(unsigned long *A, unsigned long *B, unsigned long *C, int size) {
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            unsigned long sum = 0;
            for (int k = 0; k < size; k++) {
                sum += A[i * size + k] * B[k * size + j];
            }
            // Guard by modulo 100
            C[i * size + j] = sum % 100;
        }
    }
}

// Reduction: sum all values in C
static unsigned long reduce_matrix(unsigned long *C, int size) {
    unsigned long sum = 0;
    for (int i = 0; i < size * size; i++) {
        sum += C[i];
    }
    return sum;
}

int main(int argc, char *argv[]) {
    int opt;
    int g = -1;
    int n = -1;
    bool verify = false;
    
    // Parse command line arguments
    while ((opt = getopt(argc, argv, "g:n:v")) != -1) {
        switch (opt) {
            case 'g':
                g = atoi(optarg);
                break;
            case 'n':
                n = atoi(optarg);
                break;
            case 'v':
                verify = true;
                break;
            default:
                fprintf(stderr, "Usage: %s -g <size_exponent> -n <num_trials> [-v]\n", argv[0]);
                fprintf(stderr, "  -g: matrix size is 2^g x 2^g (required)\n");
                fprintf(stderr, "  -n: number of trials (required)\n");
                fprintf(stderr, "  -v: enable verifier (optional)\n");
                exit(1);
        }
    }
    
    // Check required arguments
    if (g < 0 || n <= 0) {
        fprintf(stderr, "Error: -g and -n are required. -g must be >= 0, -n must be > 0\n");
        exit(1);
    }
    
    g_size = 1 << g;  // 2^g
    g_num_trials = n;
    g_verify = verify;
    
    // Get number of threads from environment, or use hardware concurrency
    char *threads_str = getenv("P3_NUM_THREADS");
    int num_threads;
    if (threads_str) {
        num_threads = atoi(threads_str);
        if (num_threads <= 0) {
            num_threads = 1;
        }
        printf("Using %d threads (from P3_NUM_THREADS)\n\n", num_threads);
    } else {
        num_threads = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (num_threads <= 0) {
            num_threads = 1;
        }
        // printf("Using %d threads (from hardware concurrency)\n\n", num_threads);
    }
    printf("Matrix size: %dx%d (2^%d)\n", g_size, g_size, g);
    
    // Allocate matrices
    g_A = malloc(g_size * g_size * sizeof(unsigned long));
    g_B = malloc(g_size * g_size * sizeof(unsigned long));
    unsigned long *C = malloc(g_size * g_size * sizeof(unsigned long));
    
    if (!g_A || !g_B || !C) {
        fprintf(stderr, "Failed to allocate memory for matrices\n");
        exit(1);
    }
    
    // Generate matrices A and B
    double gen_start = get_time();
    generate_matrix_A(g_A, g_size, g);
    generate_matrix_B(g_B, g_size, g);
    double gen_end = get_time();
    printf("Matrix Generation Time: %.5f\n", gen_end - gen_start);
    
    // Run verifier if enabled
    if (g_verify) {
        unsigned long *C_verify = malloc(g_size * g_size * sizeof(unsigned long));
        if (!C_verify) {
            fprintf(stderr, "Failed to allocate memory for verification matrix\n");
            exit(1);
        }
        
        single_thread_multiply(g_A, g_B, C_verify, g_size);
        g_ground_truth_sum = reduce_matrix(C_verify, g_size);
        free(C_verify);
    }
    
    printf("Matrix construction complete. Press Enter to start Matrix Multiplication benchmark...\n");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
    
    // Run trials
    double *trial_times = malloc(g_num_trials * sizeof(double));
    if (!trial_times) {
        fprintf(stderr, "Failed to allocate memory for trial times\n");
        exit(1);
    }
    
    for (int trial = 0; trial < g_num_trials; trial++) {
        // Clear result matrix
        memset(C, 0, g_size * g_size * sizeof(unsigned long));
        
        // Perform parallel multiplication
        double trial_start = get_time();
        parallel_multiply(g_A, g_B, C, g_size, num_threads);
        double trial_end = get_time();
        
        double trial_time = trial_end - trial_start;
        trial_times[trial] = trial_time;
        printf("Trial Time:          %.5f\n", trial_time);
        
        // Verify if enabled
        if (g_verify) {
            unsigned long sum = reduce_matrix(C, g_size);
            if (sum == g_ground_truth_sum) {
                printf("Verification:           PASS\n");
            } else {
                printf("Verification:           FAIL\n");
                fprintf(stderr, "ERROR: Verification failed! Trial %d: got %lu, expected %lu\n", 
                        trial + 1, sum, g_ground_truth_sum);
                exit(1);
            }
        }
    }
    
    // Calculate and print average time
    double total_time = 0.0;
    for (int i = 0; i < g_num_trials; i++) {
        total_time += trial_times[i];
    }
    double avg_time = total_time / g_num_trials;
    printf("Average Time:        %.5f\n", avg_time);
    
    // Cleanup
    free(g_A);
    free(g_B);
    free(C);
    free(trial_times);
    
    return 0;
}

