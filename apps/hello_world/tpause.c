#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <x86intrin.h>
#include <getopt.h>

static inline void tpause(uint32_t pause_cycles) {
    // Use the _tpause intrinsic from x86intrin.h
    // _tpause(hint, tsc_deadline) where:
    // - hint: pause hint (0 for normal, shallow pause)
    // - tsc_deadline: TSC deadline value
    uint64_t current_tsc = __rdtsc();
    uint64_t tsc_deadline = current_tsc + pause_cycles;
    _tpause(0, tsc_deadline);
}
/* end */

void print_help(char *argv[]) {
    printf("Usage: %s -l <num_loops> -c <pause_cycles> -h\n", argv[0]);
    printf("  -l <num_loops>    Number of times to loop\n");
    printf("  -c <pause_cycles> Number of cycles to pause in each loop\n");
    printf("  -h                Show this help message\n\n");
    printf("Example: %s -l 10 -c 1000000 -h\n", argv[0]);
}

int main(int argc, char *argv[]) {
    int num_loops = 0;
    uint32_t pause_cycles = 0;
    int opt;
    
    /* linanqinqin */
    // Parse command-line arguments using getopt
    while ((opt = getopt(argc, argv, "l:c:h")) != -1) {
        switch (opt) {
            case 'l':
                num_loops = atoi(optarg);
                if (num_loops <= 0) {
                    printf("Error: num_loops must be a positive integer\n");
                    return 1;
                }
                break;
            case 'c':
                pause_cycles = (uint32_t)atoi(optarg);
                if (pause_cycles <= 0) {
                    printf("Error: pause_cycles must be a positive integer\n");
                    return 1;
                }
                break;
            case 'h':
                print_help(argv);
                return 0;
            case '?':
                print_help(argv);
                return 1;
            default:
                print_help(argv);
                return 1;
        }
    }
    
    // Check if required arguments are provided
    if (num_loops == 0 || pause_cycles == 0) {
        printf("Error: Both -l and -c arguments are required\n");
        printf("Use -h for help\n");
        return 1;
    }
    /* end */
    
    printf("Starting tpause test:\n");
    printf("  Number of loops: %d\n", num_loops);
    printf("  Pause cycles per loop: %u\n", pause_cycles);
    printf("  Total expected cycles: %lu\n", (unsigned long)num_loops * pause_cycles);
    
    // Get initial timestamp
    uint64_t start_time = __rdtsc();
    
    /* linanqinqin */
    // Main loop: iterate for the specified number of times
    for (int i = 0; i < num_loops; i++) {
        printf("Loop %d/%d - calling tpause with %u cycles\n", i + 1, num_loops, pause_cycles);
        
        // Call tpause to pause execution for the specified number of cycles
        tpause(pause_cycles);
        
        // Optional: print progress every 10% of loops
        if ((i + 1) % (num_loops / 10 + 1) == 0) {
            printf("  Progress: %d%% complete\n", (i + 1) * 100 / num_loops);
        }
    }
    /* end */
    
    // Get final timestamp and calculate elapsed cycles
    uint64_t end_time = __rdtsc();
    uint64_t elapsed_cycles = end_time - start_time;
    
    printf("\nTest completed:\n");
    printf("  Elapsed cycles: %lu\n", elapsed_cycles);
    printf("  Expected cycles: %lu\n", (unsigned long)num_loops * pause_cycles);
    printf("  Overhead: %lu cycles (%.2f%%)\n", 
           elapsed_cycles - (unsigned long)num_loops * pause_cycles,
           (double)(elapsed_cycles - (unsigned long)num_loops * pause_cycles) * 100.0 / ((unsigned long)num_loops * pause_cycles));
    
    return 0;
}
