#!/bin/bash

# verify_shim.sh - Verify that all Caladan shim functions are correctly intercepted
#
# This script checks that all standard system/glibc functions that Caladan intercepts
# are properly overridden in a compiled binary. It uses the 'nm' command to examine
# the symbol table and looks for:
#   - T/t/W/w symbols: Functions are defined (intercepted)
#   - U symbols: Functions are undefined (not intercepted)
#
# The script verifies 47 shim functions across categories:
#   - Memory management (malloc, free, realloc, etc.)
#   - Threading (pthread_create, pthread_join, etc.)
#   - Synchronization (mutexes, condition variables, semaphores, etc.)
#   - Sleep functions (usleep, sleep, nanosleep)
#   - Thread-local storage (pthread_key_* functions)
#
# Usage: ./scripts/verify_shim.sh <path_to_binary> [--verbose]

set -e

VERBOSE=false
BINARY_PATH=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 <path_to_binary> [--verbose]"
            echo ""
            echo "Verify that all Caladan shim functions are correctly intercepted."
            echo ""
            echo "Arguments:"
            echo "  <path_to_binary>    Path to the compiled binary to check"
            echo "  --verbose, -v       Show detailed symbol information"
            echo "  --help, -h          Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0 ./apps/hello_world/hello_world"
            echo "  $0 ./apps/hello_world/hello_world --verbose"
            echo ""
            echo "Symbol types:"
            echo "  T = Text section (intercepted)"
            echo "  U = Undefined (not intercepted)"
            echo "  W/w = Weak symbol"
            exit 0
            ;;
        *)
            if [ -z "$BINARY_PATH" ]; then
                BINARY_PATH="$1"
            else
                echo "Error: Multiple binary paths specified"
                exit 1
            fi
            shift
            ;;
    esac
done

if [ -z "$BINARY_PATH" ]; then
    echo "Usage: $0 <path_to_binary> [--verbose]"
    echo "Use --help for more information"
    exit 1
fi

if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary file '$BINARY_PATH' does not exist"
    exit 1
fi

if ! command -v nm >/dev/null 2>&1; then
    echo "Error: 'nm' command not found. Please install binutils."
    exit 1
fi

# Check if the binary is executable and has symbols
if ! file "$BINARY_PATH" | grep -q "executable"; then
    echo "Warning: '$BINARY_PATH' may not be an executable binary"
fi

# Check if the binary has debug symbols or is stripped
nm_output=$(nm "$BINARY_PATH" 2>/dev/null || echo "nm failed")
if echo "$nm_output" | head -1 | grep -q "no symbols"; then
    echo "Warning: Binary appears to be stripped or has no symbols"
    echo "This may affect the accuracy of the verification"
elif echo "$nm_output" | grep -q "nm failed"; then
    echo "Error: Failed to read symbols from binary"
    echo "Make sure the binary is a valid executable file"
    exit 1
fi

# List of all shim functions to verify
SHIM_FUNCTIONS=(
    # Memory management functions
    "malloc"
    "free"
    "realloc"
    "cfree"
    "calloc"
    "memalign"
    "aligned_alloc"
    "valloc"
    "pvalloc"
    "posix_memalign"
    "__libc_free"
    "__libc_realloc"
    "__libc_calloc"
    "__libc_cfree"
    "__libc_memalign"
    "__libc_valloc"
    "__libc_pvalloc"
    "__posix_memalign"
    
    # Threading functions
    "pthread_create"
    "pthread_detach"
    "pthread_join"
    "pthread_yield"
    
    # Mutex functions
    "pthread_mutex_init"
    "pthread_mutex_lock"
    "pthread_mutex_trylock"
    "pthread_mutex_unlock"
    "pthread_mutex_destroy"
    
    # Barrier functions
    "pthread_barrier_init"
    "pthread_barrier_wait"
    "pthread_barrier_destroy"
    
    # Spinlock functions
    "pthread_spin_lock"
    "pthread_spin_trylock"
    "pthread_spin_unlock"
    
    # Condition variable functions
    "pthread_cond_init"
    "pthread_cond_signal"
    "pthread_cond_broadcast"
    "pthread_cond_wait"
    "pthread_cond_timedwait"
    "pthread_cond_destroy"
    
    # Read-write lock functions
    "pthread_rwlock_init"
    "pthread_rwlock_rdlock"
    "pthread_rwlock_tryrdlock"
    "pthread_rwlock_wrlock"
    "pthread_rwlock_trywrlock"
    "pthread_rwlock_unlock"
    "pthread_rwlock_destroy"
    
    # Semaphore functions
    "sem_init"
    "sem_destroy"
    "sem_wait"
    "sem_trywait"
    "sem_post"
    "sem_getvalue"
    
    # Sleep functions
    "usleep"
    "sleep"
    "nanosleep"
    
    # Thread-local storage functions
    "pthread_key_create"
    "pthread_key_delete"
    "pthread_getspecific"
    "pthread_setspecific"
)

echo "Verifying Caladan shim function interception for: $BINARY_PATH"
echo "=================================================================="

# Show which functions are actually referenced in the binary
if [ "$VERBOSE" = true ]; then
    echo "Functions referenced in binary:"
    nm "$BINARY_PATH" 2>/dev/null | grep -E " [UTtWw] " | grep -E "($(IFS='|'; echo "${SHIM_FUNCTIONS[*]}"))" | sort || true
    echo ""
    echo "All symbols in binary (first 20):"
    nm "$BINARY_PATH" 2>/dev/null | head -20 || true
    echo ""
    echo "Binary information:"
    file "$BINARY_PATH" || true
    echo ""
fi

# Counters for results
TOTAL_FUNCTIONS=${#SHIM_FUNCTIONS[@]}
INTERCEPTED_COUNT=0
NOT_INTERCEPTED_COUNT=0
NOT_FOUND_COUNT=0

echo "Checking $TOTAL_FUNCTIONS shim functions..."
echo "Symbol types: T=intercepted, U=not intercepted, ?=not found"
echo ""

# Show progress for large function lists
if [ $TOTAL_FUNCTIONS -gt 20 ]; then
    echo "This may take a moment for large binaries..."
fi

# Add a small delay for user experience
if [ "$VERBOSE" = false ] && [ $TOTAL_FUNCTIONS -gt 10 ]; then
    echo "Processing..."
fi

# Show a progress indicator for non-verbose mode
if [ "$VERBOSE" = false ] && [ $TOTAL_FUNCTIONS -gt 20 ]; then
    echo "Progress: ["
    progress_count=0
fi

# Add a small delay for user experience
if [ "$VERBOSE" = false ] && [ $TOTAL_FUNCTIONS -gt 10 ]; then
    sleep 0.1
fi

# Check each function
for func in "${SHIM_FUNCTIONS[@]}"; do
    # Use nm to get symbol information
    # Look for the function in the symbol table
    # T = defined in text section (intercepted)
    # U = undefined (not intercepted)
    # W = weak symbol
    # w = weak symbol (lowercase)
    
    # Escape the function name for regex
    escaped_func=$(echo "$func" | sed 's/[[\.*^$()+?{|]/\\&/g')
    symbol_info=$(nm "$BINARY_PATH" 2>/dev/null | grep -E " [TtWw] $escaped_func$" || true)
    undefined_info=$(nm "$BINARY_PATH" 2>/dev/null | grep -E " U $escaped_func$" || true)
    
    if [ -n "$symbol_info" ]; then
        # Function is defined (intercepted)
        symbol_type=$(echo "$symbol_info" | awk '{print $2}')
        if [ "$VERBOSE" = true ]; then
            echo "✓ $func - INTERCEPTED ($symbol_type)"
            echo "    Details: $symbol_info"
        else
            echo "✓ $func - INTERCEPTED ($symbol_type)"
        fi
        ((INTERCEPTED_COUNT++))
    elif [ -n "$undefined_info" ]; then
        # Function is undefined (not intercepted)
        if [ "$VERBOSE" = true ]; then
            echo "✗ $func - NOT INTERCEPTED (undefined)"
            echo "    Details: $undefined_info"
        else
            echo "✗ $func - NOT INTERCEPTED (undefined)"
        fi
        ((NOT_INTERCEPTED_COUNT++))
    else
        # Function not found in symbol table
        if [ "$VERBOSE" = true ]; then
            echo "? $func - NOT FOUND"
            echo "    No symbol found in binary (may be inlined or unused)"
        else
            echo "? $func - NOT FOUND"
        fi
        ((NOT_FOUND_COUNT++))
    fi
    
    # Update progress indicator
    if [ "$VERBOSE" = false ] && [ $TOTAL_FUNCTIONS -gt 20 ]; then
        ((progress_count++))
        if [ $((progress_count % 5)) -eq 0 ]; then
            echo -n "."
        fi
    fi
done

# Complete progress indicator
if [ "$VERBOSE" = false ] && [ $TOTAL_FUNCTIONS -gt 20 ]; then
    echo "]"
fi

echo ""
echo "=================================================================="
echo "SUMMARY:"
echo "  Total functions checked: $TOTAL_FUNCTIONS"
echo "  Intercepted (T/t/W/w): $INTERCEPTED_COUNT"
echo "  Not intercepted (U): $NOT_INTERCEPTED_COUNT"
echo "  Not found: $NOT_FOUND_COUNT"
echo ""

# Determine overall status
if [ $NOT_INTERCEPTED_COUNT -eq 0 ] && [ $NOT_FOUND_COUNT -eq 0 ]; then
    echo "✓ ALL SHIM FUNCTIONS ARE CORRECTLY INTERCEPTED"
    exit 0
elif [ $NOT_INTERCEPTED_COUNT -gt 0 ]; then
    echo "✗ SOME FUNCTIONS ARE NOT INTERCEPTED"
    echo "  This indicates that the shim layer is not working correctly"
    exit 1
else
    echo "⚠ SOME FUNCTIONS WERE NOT FOUND IN SYMBOL TABLE"
    echo "  This might be normal if the binary doesn't use all functions"
    echo "  Use --verbose to see which functions are actually referenced"
    exit 0
fi
