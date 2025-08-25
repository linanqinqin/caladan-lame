#!/usr/bin/env python3
"""
matmul.py - Verification tool for matrix multiplication results

This script parses stdin for lines containing matrix multiplication results
from threads.c and verifies them by performing the same computation.
"""

import sys
import re
import numpy as np

def generate_matrix_A(size):
    """Generate matrix A: A[i,j] = (i + j) % 100"""
    A = np.zeros((size, size), dtype=np.int64)
    for i in range(size):
        for j in range(size):
            A[i, j] = (i + j) % 100
    return A

def generate_matrix_B(size):
    """Generate matrix B: B[i,j] = (i * j + 1) % 100"""
    B = np.zeros((size, size), dtype=np.int64)
    for i in range(size):
        for j in range(size):
            B[i, j] = (i * j + 1) % 100
    return B

def matrix_multiply(A, B, size):
    """Perform matrix multiplication: C = A * B with overflow handling"""
    C = np.zeros((size, size), dtype=np.int64)
    for i in range(size):
        for j in range(size):
            C[i, j] = 0
            for k in range(size):
                # Use int64 to prevent integer overflow
                temp = int(A[i, k]) * int(B[k, j])
                C[i, j] += int(temp % 1000000)  # Keep result manageable
    return C

def calculate_matrix_sum(C):
    """Calculate sum of all elements in matrix C"""
    return np.sum(C)

def verify_matrix_multiplication(size):
    """Perform matrix multiplication and return the expected sum"""
    A = generate_matrix_A(size)
    B = generate_matrix_B(size)
    C = matrix_multiply(A, B, size)
    return calculate_matrix_sum(C)

def parse_line(line):
    """Parse a line for matrix multiplication result pattern"""
    pattern = r'\[thread_id=(\d+)\]\[size=(\d+)\]\[sum=(\d+)\]'
    match = re.search(pattern, line)
    if match:
        thread_id = int(match.group(1))
        size = int(match.group(2))
        reported_sum = int(match.group(3))
        return thread_id, size, reported_sum
    return None

def main():
    """Main verification function"""
    print("Matrix Multiplication Verification Tool")
    print("=" * 50)
    
    results = []
    line_count = 0
    
    # Read from stdin
    for line in sys.stdin:
        line_count += 1
        line = line.strip()
        
        # Parse for matrix multiplication results
        parsed = parse_line(line)
        if parsed:
            thread_id, size, reported_sum = parsed
            print(f"Found result: Thread {thread_id}, Size {size}x{size}, Reported sum: {reported_sum}")
            
            # Verify by performing the same computation
            expected_sum = verify_matrix_multiplication(size)
            
            # Compare results
            is_correct = (reported_sum == expected_sum)
            status = "✓ CORRECT" if is_correct else "✗ INCORRECT"
            
            print(f"  Expected sum: {expected_sum}")
            print(f"  Status: {status}")
            print(f"  Difference: {reported_sum - expected_sum}")
            print()
            
            results.append({
                'thread_id': thread_id,
                'size': size,
                'reported_sum': reported_sum,
                'expected_sum': expected_sum,
                'is_correct': is_correct
            })
    
    # Print verification summary
    print("=" * 50)
    print("VERIFICATION SUMMARY")
    print("=" * 50)
    
    if not results:
        print("No matrix multiplication results found in input.")
        print("Expected format: [thread_id=X][size=Y][sum=Z]")
        return
    
    total_results = len(results)
    correct_results = sum(1 for r in results if r['is_correct'])
    incorrect_results = total_results - correct_results
    
    print(f"Total results found: {total_results}")
    print(f"Correct results: {correct_results}")
    print(f"Incorrect results: {incorrect_results}")
    print(f"Accuracy: {correct_results/total_results*100:.1f}%")
    
    if incorrect_results > 0:
        print("\nIncorrect results:")
        for result in results:
            if not result['is_correct']:
                print(f"  Thread {result['thread_id']}: "
                      f"Size {result['size']}x{result['size']}, "
                      f"Reported {result['reported_sum']}, "
                      f"Expected {result['expected_sum']}, "
                      f"Diff {result['reported_sum'] - result['expected_sum']}")
    
    if correct_results == total_results:
        print("\nAll results are correct!")
    else:
        print(f"\nFound {incorrect_results} incorrect result(s)")

if __name__ == "__main__":
    main()
