/*
 * hello_world.c - A simple hello world application for Caladan
 * 
 * This demonstrates how to write a standard POSIX application that
 * runs transparently on Caladan. The application uses standard C/POSIX
 * APIs and is linked against Caladan's libraries instead of glibc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

int main(int argc, char *argv[])
{
    printf("Hello, World from Caladan!\n");
    printf("This is a standard POSIX application running on Caladan\n");
    printf("Arguments: argc=%d\n", argc);
    
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d]: %s\n", i, argv[i]);
    }
    
    printf("Process ID: %d\n", getpid());
    printf("Parent Process ID: %d\n", getppid());
    
    // Demonstrate some basic POSIX functionality
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        printf("Hostname: %s\n", hostname);
    }
    
    printf("Hello world application completed successfully!\n");
    return 0;
} 