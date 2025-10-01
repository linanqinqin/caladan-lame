/*
 * matmul.c - Verification tool for matrix multiplication results
 * 
 * This program parses stdin for lines containing matrix multiplication results
 * from threads.c and verifies them by performing the same computation.
 * Much faster than the Python version!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <stdbool.h>

#define MAX_LINE_LENGTH 1024

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
    }
}

// Function to calculate matrix multiplication result sum
long long calculate_matrix_sum(int *C, int size) {
    long long sum = 0;
    for (int i = 0; i < size * size; i++) {
        sum += C[i];
    }
    return sum;
}

// Function to verify matrix multiplication and return expected sum
long long verify_matrix_multiplication(int size) {
    int *A = malloc(size * size * sizeof(int));
    int *B = malloc(size * size * sizeof(int));
    int *C = malloc(size * size * sizeof(int));
    
    if (!A || !B || !C) {
        fprintf(stderr, "Failed to allocate memory for %dx%d matrices\n", size, size);
        free(A);
        free(B);
        free(C);
        return -1;
    }
    
    generate_matrix_A(A, size);
    generate_matrix_B(B, size);
    matrix_multiply(A, B, C, size);
    long long sum = calculate_matrix_sum(C, size);
    
    free(A);
    free(B);
    free(C);
    
    return sum;
}

// Function to parse a line for matrix multiplication result pattern
// Returns 1 if pattern found, 0 otherwise
int parse_line(const char *line, int *thread_id, int *size, long long *reported_sum) {
    // Pattern: [thread_id=%d][size=%d][sum=%lld]
    if (sscanf(line, "%*[^[][thread_id=%d][size=%d][sum=%lld]", 
               thread_id, size, reported_sum) == 3) {
        return 1;
    }
    return 0;
}

static volatile sig_atomic_t g_interrupted = 0;

static int g_total_results = 0;
static int g_correct_results = 0;
static int g_incorrect_results = 0;

struct bad_result {
    int thread_id;
    int size;
    long long reported_sum;
    long long expected_sum;
};

static struct bad_result *g_bad = NULL;
static int g_bad_cap = 0;
static int g_bad_len = 0;

static void ensure_bad_capacity(void) {
    if (g_bad_len < g_bad_cap) return;
    int new_cap = g_bad_cap == 0 ? 16 : g_bad_cap * 2;
    struct bad_result *nb = realloc(g_bad, new_cap * sizeof(*nb));
    if (!nb) return; // best-effort; if realloc fails, we just skip recording
    g_bad = nb;
    g_bad_cap = new_cap;
}

static void sigint_handler(int signo) {
    (void)signo;
    g_interrupted = 1;
}

static void print_summary(void) {
    printf("==================================================\n");
    printf("VERIFICATION SUMMARY\n");
    printf("==================================================\n");

    if (g_total_results == 0) {
        printf("No matrix multiplication results found in input.\n");
        printf("Expected format: [thread_id=X][size=Y][sum=Z]\n");
        return;
    }

    printf("Total results found: %d\n", g_total_results);
    printf("Correct results: %d\n", g_correct_results);
    printf("Incorrect results: %d\n", g_incorrect_results);
    printf("Accuracy: %.1f%%\n", g_total_results ? (double)g_correct_results / g_total_results * 100.0 : 0.0);

    if (g_incorrect_results > 0) {
        printf("\nIncorrect results (up to %d shown):\n", g_bad_len);
        for (int i = 0; i < g_bad_len; i++) {
            printf("  Thread %d: Size %dx%d, Reported %lld, Expected %lld, Diff %lld\n",
                   g_bad[i].thread_id, g_bad[i].size, g_bad[i].size,
                   g_bad[i].reported_sum, g_bad[i].expected_sum,
                   g_bad[i].reported_sum - g_bad[i].expected_sum);
        }
    } else {
        printf("\nAll results are correct!\n");
    }
}

int main(int argc, char *argv[])
{
    printf("Matrix Multiplication Verification Tool (C version)\n");
    printf("==================================================\n");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    char line[MAX_LINE_LENGTH];
    
    // Read from stdin line by line
    while (fgets(line, sizeof(line), stdin)) {
        if (g_interrupted) break;

        int thread_id, size;
        long long reported_sum;
        
        // Parse for matrix multiplication results
        if (parse_line(line, &thread_id, &size, &reported_sum)) {
            printf("Found result: Thread %d, Size %dx%d, Reported sum: %lld\n", 
                   thread_id, size, size, reported_sum);
            
            // Verify by performing the same computation
            long long expected_sum = verify_matrix_multiplication(size);
            
            if (expected_sum == -1) {
                printf("  ERROR: Failed to verify computation\n\n");
                g_incorrect_results++;
            } else {
                // Compare results
                int is_correct = (reported_sum == expected_sum);
                const char *status = is_correct ? "CORRECT" : "INCORRECT";
                
                printf("  Expected sum: %lld\n", expected_sum);
                printf("  Status: %s\n", status);
                printf("\n");
                
                if (is_correct) {
                    g_correct_results++;
                } else {
                    g_incorrect_results++;
                    ensure_bad_capacity();
                    if (g_bad_len < g_bad_cap) {
                        g_bad[g_bad_len].thread_id = thread_id;
                        g_bad[g_bad_len].size = size;
                        g_bad[g_bad_len].reported_sum = reported_sum;
                        g_bad[g_bad_len].expected_sum = expected_sum;
                        g_bad_len++;
                    }
                }
            }
            
            g_total_results++;
        }
    }
    
    // Print verification summary
    print_summary();
    
    free(g_bad);
    
    return 0;
}
