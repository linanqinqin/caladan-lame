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

#define MAX_LINE_LENGTH 1024
#define MAX_MATRIX_SIZE 1024

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

int main(int argc, char *argv[])
{
    printf("Matrix Multiplication Verification Tool (C version)\n");
    printf("==================================================\n");
    
    char line[MAX_LINE_LENGTH];
    int total_results = 0;
    int correct_results = 0;
    int incorrect_results = 0;
    
    // Read from stdin line by line
    while (fgets(line, sizeof(line), stdin)) {
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
                incorrect_results++;
            } else {
                // Compare results
                int is_correct = (reported_sum == expected_sum);
                const char *status = is_correct ? "CORRECT" : "INCORRECT";
                
                printf("  Expected sum: %lld\n", expected_sum);
                printf("  Status: %s\n", status);
                // printf("  Difference: %lld\n", reported_sum - expected_sum);
                printf("\n");
                
                if (is_correct) {
                    correct_results++;
                } else {
                    incorrect_results++;
                }
            }
            
            total_results++;
        }
    }
    
    // Print verification summary
    printf("==================================================\n");
    printf("VERIFICATION SUMMARY\n");
    printf("==================================================\n");
    
    if (total_results == 0) {
        printf("No matrix multiplication results found in input.\n");
        printf("Expected format: [thread_id=X][size=Y][sum=Z]\n");
        return 0;
    }
    
    printf("Total results found: %d\n", total_results);
    printf("Correct results: %d\n", correct_results);
    printf("Incorrect results: %d\n", incorrect_results);
    printf("Accuracy: %.1f%%\n", (double)correct_results / total_results * 100.0);
    
    if (incorrect_results > 0) {
        printf("\nIncorrect results found!\n");
    }
    
    if (correct_results == total_results) {
        printf("\nAll results are correct!\n");
    } else {
        printf("\nFound %d incorrect result(s)\n", incorrect_results);
    }
    
    return 0;
}
