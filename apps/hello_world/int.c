/*
 * int.c - Test program for LAME interrupt handling via int 0x1f
 * 
 * This program tests the LAME interrupt handler by calling int 0x1f
 * and verifying that execution can safely return after the interrupt.
 * 
 * Based on LAME design: Software-initiated interrupts via INT instruction
 * using the unused IDT entry 0x1f for controlled testing environment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    printf("=== LAME Interrupt Test Program ===\n");
    printf("Testing int 0x1f interrupt handling...\n");
    printf("Process ID: %d\n", getpid());
    
    printf("\nBefore int 0x1f call...\n");
    printf("About to execute: int 0x1f\n");
    
    // Call the LAME interrupt (int 0x1f)
    // This should trigger the LAME handler if properly set up
    __asm__ volatile("int $0x1f");
    
    printf("\nAfter int 0x1f call...\n");
    printf("Successfully returned from interrupt!\n");
    
    printf("\nTest completed successfully.\n");
    printf("LAME interrupt handler is working correctly.\n");
    
    return 0;
} 