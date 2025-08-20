/*
 * matmul.c - Ground truth matrix multiplication program
 * 
 * This is a standard single-threaded C program that performs the exact same
 * deterministic matrix multiplication as the Caladan threads.c program.
 * It serves as ground truth for verifying the final sum.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MATRIX_SIZE 128

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
long long calculate_matrix_sum(int *A, int *B, int *C, int size) {
    long long sum = 0;
    for (int i = 0; i < size * size; i++) {
        sum += C[i];
    }
    return sum;
}

int main(int argc, char *argv[])
{
    printf("Ground Truth Matrix Multiplication Calculator\n");
    printf("Matrix size: %dx%d\n\n", MATRIX_SIZE, MATRIX_SIZE);
    
    // Allocate matrices
    int *A = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(int));
    int *B = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(int));
    int *C = malloc(MATRIX_SIZE * MATRIX_SIZE * sizeof(int));
    
    if (!A || !B || !C) {
        printf("Failed to allocate memory for matrices\n");
        free(A);
        free(B);
        free(C);
        return 1;
    }
    
    // Generate deterministic matrices
    generate_matrix_A(A, MATRIX_SIZE);
    generate_matrix_B(B, MATRIX_SIZE);
    
    // Perform matrix multiplication
    matrix_multiply(A, B, C, MATRIX_SIZE);
    
    // Calculate and print result sum
    long long result_sum = calculate_matrix_sum(A, B, C, MATRIX_SIZE);
    printf("Final result sum: %lld\n", result_sum);
    
    // Clean up
    free(A);
    free(B);
    free(C);
    
    return 0;
}
