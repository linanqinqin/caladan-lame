/*
 * parse_lame.c - LAME scheduling log parser
 * 
 * This program parses LAME scheduling logs from stdin and groups them by uthread
 * to show the lifetime events of each uthread.
 * 
 * Usage: ./hello_world | ./parse_lame
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_LINE_LEN 1024
#define MAX_UTHREADS 100
#define MAX_EVENTS_PER_UTHREAD 1000
#define MAX_FILTERED_LINES 10000

typedef struct {
    char *uthread_addr;
    char *events[MAX_EVENTS_PER_UTHREAD];
    int event_count;
    bool active;
} uthread_info_t;

typedef struct {
    uthread_info_t uthreads[MAX_UTHREADS];
    int uthread_count;
    char *filtered_lines[MAX_FILTERED_LINES];
    int filtered_line_count;
} parse_state_t;

/* Initialize parse state */
void init_parse_state(parse_state_t *state) {
    state->uthread_count = 0;
    state->filtered_line_count = 0;
    for (int i = 0; i < MAX_UTHREADS; i++) {
        state->uthreads[i].uthread_addr = NULL;
        state->uthreads[i].event_count = 0;
        state->uthreads[i].active = false;
    }
    for (int i = 0; i < MAX_FILTERED_LINES; i++) {
        state->filtered_lines[i] = NULL;
    }
}

/* Find or create uthread info */
uthread_info_t *get_uthread_info(parse_state_t *state, const char *uthread_addr) {
    // Look for existing uthread
    for (int i = 0; i < state->uthread_count; i++) {
        if (state->uthreads[i].uthread_addr && 
            strcmp(state->uthreads[i].uthread_addr, uthread_addr) == 0) {
            return &state->uthreads[i];
        }
    }
    
    // Create new uthread if space available
    if (state->uthread_count < MAX_UTHREADS) {
        uthread_info_t *uthread = &state->uthreads[state->uthread_count];
        uthread->uthread_addr = strdup(uthread_addr);
        uthread->event_count = 0;
        uthread->active = false;
        state->uthread_count++;
        return uthread;
    }
    
    return NULL;
}

/* Check if a line is a Caladan log line (has timestamp and CPU) */
bool is_caladan_log_line(const char *line) {
    // Look for timestamp pattern [  x.xxxxxx]
    const char *timestamp_start = strchr(line, '[');
    if (!timestamp_start) {
        return false;
    }
    
    // Look for "CPU" after timestamp
    const char *cpu_pattern = strstr(line, "CPU");
    if (!cpu_pattern) {
        return false;
    }
    
    // Make sure CPU comes after timestamp
    return cpu_pattern > timestamp_start;
}

/* Add filtered line to state */
void add_filtered_line(parse_state_t *state, const char *line) {
    if (state->filtered_line_count >= MAX_FILTERED_LINES) {
        return; // Too many lines
    }
    
    // Remove newline if present
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
        len--;
    }
    
    char *filtered_line = malloc(len + 1);
    if (!filtered_line) return;
    
    strncpy(filtered_line, line, len);
    filtered_line[len] = '\0';
    
    state->filtered_lines[state->filtered_line_count] = filtered_line;
    state->filtered_line_count++;
}

/* Print filtered program output */
void print_filtered_output(const parse_state_t *state) {
    printf("=== FILTERED PROGRAM OUTPUT ===\n");
    for (int i = 0; i < state->filtered_line_count; i++) {
        printf("%s\n", state->filtered_lines[i]);
    }
    printf("==============================\n\n");
}

/* Parse a line and extract LAME scheduling information */
bool parse_lame_line(const char *line, char *uthread_addr, char *event_type, char *details) {
    // Extract timestamp from the beginning of the line
    char timestamp[32] = "";
    const char *timestamp_start = strchr(line, '[');
    const char *timestamp_end = NULL;
    
    if (timestamp_start) {
        timestamp_end = strchr(timestamp_start, ']');
        if (timestamp_end) {
            int timestamp_len = timestamp_end - timestamp_start + 1; // Include brackets
            if (timestamp_len > 0 && timestamp_len < sizeof(timestamp)) {
                strncpy(timestamp, timestamp_start, timestamp_len);
                timestamp[timestamp_len] = '\0';
            }
        }
    }
    
    // Look for LAME scheduling pattern
    const char *lame_pattern = "[LAME][sched ";
    const char *lame_start = strstr(line, lame_pattern);
    if (!lame_start) {
        return false;
    }
    
    // Extract event type (ON/OFF)
    const char *event_start = lame_start + strlen(lame_pattern);
    const char *event_end = strchr(event_start, ']');
    if (!event_end) {
        return false;
    }
    
    int event_len = event_end - event_start;
    if (event_len >= 10) return false; // Sanity check
    strncpy(event_type, event_start, event_len);
    event_type[event_len] = '\0';
    
    // Look for function name in square brackets
    const char *func_start = strchr(event_end, '[');
    const char *func_end = NULL;
    char func_name[64] = "";
    
    if (func_start) {
        func_end = strchr(func_start, ']');
        if (func_end) {
            int func_len = func_end - func_start - 1; // -1 to exclude the brackets
            if (func_len > 0 && func_len < sizeof(func_name) - 1) {
                strncpy(func_name, func_start + 1, func_len);
                func_name[func_len] = '\0';
            }
        }
    }
    
    // Look for uthread address
    const char *uthread_pattern = "uthread ";
    const char *uthread_start = strstr(line, uthread_pattern);
    if (!uthread_start) {
        return false;
    }
    
    uthread_start += strlen(uthread_pattern);
    const char *uthread_end = strchr(uthread_start, ' ');
    if (!uthread_end) {
        return false;
    }
    
    int uthread_len = uthread_end - uthread_start;
    if (uthread_len >= 20) return false; // Sanity check
    strncpy(uthread_addr, uthread_start, uthread_len);
    uthread_addr[uthread_len] = '\0';
    
    // Extract details (everything after uthread address)
    const char *details_start = uthread_end + 1;
    const char *details_end = strchr(details_start, '\n');
    if (details_end) {
        int details_len = details_end - details_start;
        if (details_len >= 200) details_len = 199; // Limit length
        strncpy(details, details_start, details_len);
        details[details_len] = '\0';
    } else {
        strcpy(details, details_start);
    }
    
    // Prepend timestamp and function name to details
    char temp_details[256];
    strcpy(temp_details, details);
    
    if (strlen(func_name) > 0) {
        snprintf(details, 256, "%s [%s] %s", timestamp, func_name, temp_details);
    } else {
        snprintf(details, 256, "%s %s", timestamp, temp_details);
    }
    
    return true;
}

/* Add event to uthread */
void add_event(uthread_info_t *uthread, const char *event_type, const char *details) {
    if (uthread->event_count >= MAX_EVENTS_PER_UTHREAD) {
        return; // Too many events
    }
    
    // Create event string with timestamp first
    char *event = malloc(256);
    if (!event) return;
    
    // Extract timestamp from details (it's the first field)
    char timestamp[32] = "";
    const char *timestamp_end = strchr(details, ']');
    if (timestamp_end) {
        int timestamp_len = timestamp_end - details + 1;
        if (timestamp_len > 0 && timestamp_len < sizeof(timestamp)) {
            strncpy(timestamp, details, timestamp_len);
            timestamp[timestamp_len] = '\0';
        }
    }
    
    // Format: timestamp [event_type] [function_name] rest_of_details
    const char *rest_of_details = details;
    if (strlen(timestamp) > 0) {
        rest_of_details = details + strlen(timestamp) + 1; // Skip timestamp and space
    }
    
    snprintf(event, 256, "%s [%s] %s", timestamp, event_type, rest_of_details);
    uthread->events[uthread->event_count] = event;
    uthread->event_count++;
    
    // Update active status
    if (strcmp(event_type, "ON") == 0) {
        uthread->active = true;
    } else if (strcmp(event_type, "OFF") == 0) {
        uthread->active = false;
    }
}

/* Print uthread summary */
void print_uthread_summary(const uthread_info_t *uthread) {
    printf("\n=== UTHREAD %s ===\n", uthread->uthread_addr);
    printf("Status: %s\n", uthread->active ? "ACTIVE" : "INACTIVE");
    printf("Total Events: %d\n", uthread->event_count);
    printf("Event Timeline:\n");
    
    for (int i = 0; i < uthread->event_count; i++) {
        printf("  %2d: %s\n", i + 1, uthread->events[i]);
    }
    printf("==================\n");
}

/* Print overall summary */
void print_summary(const parse_state_t *state) {
    printf("\n=== LAME SCHEDULING SUMMARY ===\n");
    printf("Total UTHREADs: %d\n", state->uthread_count);
    
    int active_count = 0;
    for (int i = 0; i < state->uthread_count; i++) {
        if (state->uthreads[i].active) {
            active_count++;
        }
    }
    printf("Active UTHREADs: %d\n", active_count);
    printf("Inactive UTHREADs: %d\n", state->uthread_count - active_count);
    printf("==============================\n");
}

/* Clean up memory */
void cleanup_parse_state(parse_state_t *state) {
    for (int i = 0; i < state->uthread_count; i++) {
        uthread_info_t *uthread = &state->uthreads[i];
        if (uthread->uthread_addr) {
            free(uthread->uthread_addr);
        }
        for (int j = 0; j < uthread->event_count; j++) {
            if (uthread->events[j]) {
                free(uthread->events[j]);
            }
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
    char uthread_addr[32];
    char event_type[16];
    char details[256];
    
    init_parse_state(&state);
    
    printf("LAME Scheduling Log Parser\n");
    printf("Reading from stdin...\n\n");
    
    // Read lines from stdin
    while (fgets(line, sizeof(line), stdin)) {
        // Filter Caladan log lines
        if (is_caladan_log_line(line)) {
            // Parse LAME scheduling events
            if (parse_lame_line(line, uthread_addr, event_type, details)) {
                uthread_info_t *uthread = get_uthread_info(&state, uthread_addr);
                if (uthread) {
                    add_event(uthread, event_type, details);
                    printf("Parsed: %s -> %s (%s)\n", uthread_addr, event_type, details);
                }
            }
        } else {
            // Regular program output
            add_filtered_line(&state, line);
        }
    }
    
    // Print filtered program output first
    print_filtered_output(&state);
    
    // Print summary
    print_summary(&state);
    
    // Print detailed uthread information
    for (int i = 0; i < state.uthread_count; i++) {
        print_uthread_summary(&state.uthreads[i]);
    }
    
    // Cleanup
    cleanup_parse_state(&state);
    
    return 0;
}
