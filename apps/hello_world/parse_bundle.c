/*
 * parse_bundle.c - LAME bundle log parser
 * 
 * This program parses LAME bundle logs from stdin and validates bundle consistency
 * and lifecycle per kthread.
 * 
 * Usage: ./hello_world | ./parse_bundle
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_LINE_LEN 1024
#define MAX_KTHREADS 100
#define MAX_BUNDLE_SIZE 32
#define MAX_BUNDLE_HISTORY 1000

typedef struct {
    char *bundle_str;
    int size;
    int used;
    int active;
    int enabled;
    char *uthreads[MAX_BUNDLE_SIZE];
    int uthread_count;
} bundle_info_t;

typedef struct {
    int kthread_id;
    bundle_info_t bundles[MAX_BUNDLE_HISTORY];
    int bundle_count;
    bool validation_errors;
    bool *entry_errors;  // Track which entries have errors
} kthread_bundle_t;

typedef struct {
    kthread_bundle_t kthreads[MAX_KTHREADS];
    int kthread_count;
    char *filtered_lines[MAX_BUNDLE_HISTORY];
    int filtered_line_count;
} parse_state_t;

/* Initialize parse state */
void init_parse_state(parse_state_t *state) {
    state->kthread_count = 0;
    state->filtered_line_count = 0;
    for (int i = 0; i < MAX_KTHREADS; i++) {
        state->kthreads[i].kthread_id = -1;
        state->kthreads[i].bundle_count = 0;
        state->kthreads[i].validation_errors = false;
        state->kthreads[i].entry_errors = NULL; // Initialize entry_errors
    }
    for (int i = 0; i < MAX_BUNDLE_HISTORY; i++) {
        state->filtered_lines[i] = NULL;
    }
}

/* Find or create kthread bundle info */
kthread_bundle_t *get_kthread_bundle(parse_state_t *state, int kthread_id) {
    // Look for existing kthread
    for (int i = 0; i < state->kthread_count; i++) {
        if (state->kthreads[i].kthread_id == kthread_id) {
            return &state->kthreads[i];
        }
    }
    
    // Create new kthread if space available
    if (state->kthread_count < MAX_KTHREADS) {
        kthread_bundle_t *kthread = &state->kthreads[state->kthread_count];
        kthread->kthread_id = kthread_id;
        kthread->bundle_count = 0;
        kthread->validation_errors = false;
        kthread->entry_errors = NULL; // Initialize entry_errors for new kthread
        state->kthread_count++;
        return kthread;
    }
    
    return NULL;
}

/* Parse bundle string to extract uthread addresses */
int parse_bundle_string(const char *bundle_str, char *uthreads[], int max_uthreads) {
    int count = 0;
    const char *current = bundle_str;
    
    // Skip opening '<'
    if (*current == '<') current++;
    
    while (*current && count < max_uthreads) {
        // Skip whitespace and commas
        while (*current && (*current == ' ' || *current == ',')) current++;
        if (!*current) break;
        
        // Check for "(nil)"
        if (strncmp(current, "(nil)", 5) == 0) {
            uthreads[count] = strdup("(nil)");
            current += 5;
            count++;
            continue;
        }
        
        // Extract hex address
        if (*current == '0' && *(current + 1) == 'x') {
            const char *start = current;
            current += 2; // Skip "0x"
            
            // Find end of hex number
            while (*current && ((*current >= '0' && *current <= '9') || 
                               (*current >= 'a' && *current <= 'f') ||
                               (*current >= 'A' && *current <= 'F'))) {
                current++;
            }
            
            int len = current - start;
            if (len > 0) {
                uthreads[count] = malloc(len + 1);
                if (uthreads[count]) {
                    strncpy(uthreads[count], start, len);
                    uthreads[count][len] = '\0';
                    count++;
                }
            }
        } else {
            current++; // Skip unknown character
        }
    }
    
    return count;
}

/* Parse a LAME bundle line */
bool parse_bundle_line(const char *line, bundle_info_t *bundle) {
    // Look for LAME bundle pattern
    const char *bundle_pattern = "[LAME][BUNDLE]";
    const char *bundle_start = strstr(line, bundle_pattern);
    if (!bundle_start) {
        return false;
    }
    
    // Initialize bundle info
    bundle->size = -1;
    bundle->used = -1;
    bundle->active = -1;
    bundle->enabled = -1;
    bundle->bundle_str = NULL;
    bundle->uthread_count = 0;
    
    // Parse field-value pairs
    const char *current_pos = bundle_start + strlen(bundle_pattern);
    
    while (*current_pos) {
        // Look for next field [field:value]
        const char *field_start = strchr(current_pos, '[');
        if (!field_start) break;
        
        const char *field_end = strchr(field_start, ']');
        if (!field_end) break;
        
        // Extract field-value pair
        int field_len = field_end - field_start - 1;
        if (field_len <= 0) {
            current_pos = field_end + 1;
            continue;
        }
        
        char field_value[128];
        if (field_len >= sizeof(field_value)) field_len = sizeof(field_value) - 1;
        strncpy(field_value, field_start + 1, field_len);
        field_value[field_len] = '\0';
        
        // Parse field:value
        const char *colon = strchr(field_value, ':');
        if (colon) {
            char field_name[32];
            char field_val[96];
            
            int field_name_len = colon - field_value;
            int field_val_len = strlen(field_value) - field_name_len - 1;
            
            if (field_name_len > 0 && field_name_len < sizeof(field_name) && 
                field_val_len > 0 && field_val_len < sizeof(field_val)) {
                
                strncpy(field_name, field_value, field_name_len);
                field_name[field_name_len] = '\0';
                strncpy(field_val, colon + 1, field_val_len);
                field_val[field_val_len] = '\0';
                
                // Store values based on field name
                if (strcmp(field_name, "kthread") == 0) {
                    // Skip kthread field for now (we'll get it from the bundle info)
                } else if (strcmp(field_name, "size") == 0) {
                    bundle->size = atoi(field_val);
                } else if (strcmp(field_name, "used") == 0) {
                    bundle->used = atoi(field_val);
                } else if (strcmp(field_name, "active") == 0) {
                    bundle->active = atoi(field_val);
                } else if (strcmp(field_name, "enabled") == 0) {
                    bundle->enabled = atoi(field_val);
                } else if (strcmp(field_name, "bundle") == 0) {
                    bundle->bundle_str = strdup(field_val);
                }
            }
        }
        
        current_pos = field_end + 1;
    }
    
    // Parse bundle string to extract uthreads
    if (bundle->bundle_str) {
        bundle->uthread_count = parse_bundle_string(bundle->bundle_str, bundle->uthreads, MAX_BUNDLE_SIZE);
    }
    
    return (bundle->size >= 0 && bundle->used >= 0 && bundle->bundle_str != NULL);
}

/* Validate bundle consistency */
bool validate_bundle(const bundle_info_t *bundle, int line_num) {
    bool valid = true;
    
    // Check 1: used <= size
    if (bundle->used > bundle->size) {
        printf("ERROR line %d: used (%d) > size (%d)\n", line_num, bundle->used, bundle->size);
        valid = false;
    }
    
    // Check 2: bundle field has exactly "used" many non-nil uthreads
    int non_nil_count = 0;
    for (int i = 0; i < bundle->uthread_count; i++) {
        if (strcmp(bundle->uthreads[i], "(nil)") != 0) {
            non_nil_count++;
        }
    }
    
    if (non_nil_count != bundle->used) {
        printf("ERROR line %d: bundle has %d non-nil uthreads but used=%d\n", 
               line_num, non_nil_count, bundle->used);
        valid = false;
    }
    
    return valid;
}

/* Validate kthread bundle lifecycle */
bool validate_kthread_lifecycle(kthread_bundle_t *kthread) {
    bool valid = true;
    
    // Allocate entry_errors array if not already allocated
    if (!kthread->entry_errors && kthread->bundle_count > 0) {
        kthread->entry_errors = calloc(kthread->bundle_count, sizeof(bool));
    }
    
    if (kthread->bundle_count < 2) {
        return true; // Need at least 2 entries to check growth
    }
    
    // Check 1: bundle never grows for more than two consecutive times
    int consecutive_growth = 0;
    for (int i = 1; i < kthread->bundle_count; i++) {
        if (kthread->bundles[i].used > kthread->bundles[i-1].used) {
            consecutive_growth++;
            if (consecutive_growth > 2) {
                printf("ERROR kthread %d: bundle grew for %d consecutive times (entry %d)\n", 
                       kthread->kthread_id, consecutive_growth, i + 1);
                valid = false;
                if (kthread->entry_errors) {
                    kthread->entry_errors[i] = true;
                }
            }
        } else {
            consecutive_growth = 0;
        }
    }
    
    // Check 2: bundle ends up empty
    if (kthread->bundle_count > 0) {
        const bundle_info_t *last_bundle = &kthread->bundles[kthread->bundle_count - 1];
        if (last_bundle->used != 0) {
            printf("ERROR kthread %d: bundle does not end empty (used=%d in last entry)\n", 
                   kthread->kthread_id, last_bundle->used);
            valid = false;
            if (kthread->entry_errors) {
                kthread->entry_errors[kthread->bundle_count - 1] = true;
            }
        }
    }
    
    return valid;
}

/* Add bundle to kthread history */
void add_bundle_to_kthread(kthread_bundle_t *kthread, const bundle_info_t *bundle) {
    if (kthread->bundle_count >= MAX_BUNDLE_HISTORY) {
        printf("WARNING: Too many bundle entries for kthread %d\n", kthread->kthread_id);
        return;
    }
    
    // Allocate entry_errors array if this is the first entry
    if (kthread->bundle_count == 0) {
        kthread->entry_errors = calloc(MAX_BUNDLE_HISTORY, sizeof(bool));
    }
    
    bundle_info_t *new_bundle = &kthread->bundles[kthread->bundle_count];
    
    // Copy bundle info
    new_bundle->size = bundle->size;
    new_bundle->used = bundle->used;
    new_bundle->active = bundle->active;
    new_bundle->enabled = bundle->enabled;
    new_bundle->bundle_str = strdup(bundle->bundle_str);
    new_bundle->uthread_count = bundle->uthread_count;
    
    // Copy uthread pointers
    for (int i = 0; i < bundle->uthread_count; i++) {
        new_bundle->uthreads[i] = strdup(bundle->uthreads[i]);
    }
    
    kthread->bundle_count++;
}

/* Print kthread bundle summary */
void print_kthread_summary(const kthread_bundle_t *kthread) {
    printf("\n=== KTHREAD %d ===\n", kthread->kthread_id);
    printf("Total Bundle Entries: %d\n", kthread->bundle_count);
    printf("Validation Status: %s\n", kthread->validation_errors ? "FAILED" : "PASSED");
    
    if (kthread->validation_errors && kthread->entry_errors) {
        printf("Bundle History with Errors:\n");
        for (int i = 0; i < kthread->bundle_count; i++) {
            if (kthread->entry_errors[i]) {
                const bundle_info_t *bundle = &kthread->bundles[i];
                printf("  %2d: size=%d used=%d active=%d enabled=%d bundle=%s [ERROR]\n", 
                       i + 1, bundle->size, bundle->used, bundle->active, bundle->enabled, bundle->bundle_str);
            }
        }
    }
    printf("==================\n");
}

/* Clean up memory */
void cleanup_parse_state(parse_state_t *state) {
    for (int i = 0; i < state->kthread_count; i++) {
        kthread_bundle_t *kthread = &state->kthreads[i];
        for (int j = 0; j < kthread->bundle_count; j++) {
            bundle_info_t *bundle = &kthread->bundles[j];
            if (bundle->bundle_str) {
                free(bundle->bundle_str);
            }
            for (int k = 0; k < bundle->uthread_count; k++) {
                if (bundle->uthreads[k]) {
                    free(bundle->uthreads[k]);
                }
            }
        }
        if (kthread->entry_errors) {
            free(kthread->entry_errors);
        }
    }
    
    for (int i = 0; i < state->filtered_line_count; i++) {
        if (state->filtered_lines[i]) {
            free(state->filtered_lines[i]);
        }
    }
}

int main() {
    parse_state_t state;
    char line[MAX_LINE_LEN];
    int line_num = 0;
    
    init_parse_state(&state);
    
    printf("LAME Bundle Log Parser\n");
    printf("Reading from stdin...\n\n");
    
    // Read lines from stdin
    while (fgets(line, sizeof(line), stdin)) {
        line_num++;
        
        // Look for LAME bundle lines
        if (strstr(line, "[LAME][BUNDLE]")) {
            bundle_info_t bundle;
            if (parse_bundle_line(line, &bundle)) {
                // Extract kthread ID from the line
                const char *kthread_pattern = "[kthread:";
                const char *kthread_start = strstr(line, kthread_pattern);
                if (kthread_start) {
                    kthread_start += strlen(kthread_pattern);
                    int kthread_id = atoi(kthread_start);
                    
                    // Validate bundle consistency
                    if (!validate_bundle(&bundle, line_num)) {
                        printf("Bundle validation failed at line %d\n", line_num);
                        // Mark this entry as having an error
                        if (kthread && kthread->entry_errors) {
                            kthread->entry_errors[kthread->bundle_count] = true;
                        }
                    }
                    
                    // Add to kthread history
                    kthread_bundle_t *kthread = get_kthread_bundle(&state, kthread_id);
                    if (kthread) {
                        add_bundle_to_kthread(kthread, &bundle);
                    }
                }
            }
            
            // Free bundle string
            if (bundle.bundle_str) {
                free(bundle.bundle_str);
            }
            for (int i = 0; i < bundle.uthread_count; i++) {
                if (bundle.uthreads[i]) {
                    free(bundle.uthreads[i]);
                }
            }
        }
    }
    
    // Validate kthread lifecycles
    printf("\n=== BUNDLE LIFECYCLE VALIDATION ===\n");
    for (int i = 0; i < state.kthread_count; i++) {
        kthread_bundle_t *kthread = &state.kthreads[i];
        if (!validate_kthread_lifecycle(kthread)) {
            kthread->validation_errors = true;
        }
    }
    
    // Print summaries
    printf("\n=== BUNDLE SUMMARY ===\n");
    printf("Total KTHREADs: %d\n", state.kthread_count);
    
    for (int i = 0; i < state.kthread_count; i++) {
        print_kthread_summary(&state.kthreads[i]);
    }
    
    // Cleanup
    cleanup_parse_state(&state);
    
    return 0;
}
