#!/usr/bin/env bash
#
# cpu-governor.sh
# Simple all-core CPU scaling governor switcher.
#
# Options:
#   -p   performance
#   -s   powersave
#   -b   balanced (schedutil)
#   -o   ondemand
#   -c   conservative
#   -g   show current governors
#   -h   help
#

set -euo pipefail

print_usage() {
    cat <<EOF
Usage: $0 [OPTION]

Options (choose one):
  -p    Set governor to performance
  -s    Set governor to powersave
  -b    Set governor to schedutil (balanced)
  -o    Set governor to ondemand
  -c    Set governor to conservative
  -g    Show current governors for all CPUs
  -h    Show this help message

Examples:
  $0 -p    # lock all CPUs to performance
  $0 -b    # restore balanced (schedutil)
  $0 -g    # inspect current per-CPU governors
EOF
}

require_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "Re-running as root via sudo..."
        exec sudo "$0" "$@"
    fi
}

show_governors() {
    echo "Current scaling governors:"
    for gov_file in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [[ -f "$gov_file" ]] || continue
        cpu_name=$(basename "$(dirname "$gov_file")")
        cur_gov=$(<"$gov_file")
        echo "  $cpu_name: $cur_gov"
    done
}

set_governor_all() {
    local target_gov="$1"

    echo "Setting governor to '$target_gov' for all CPUs..."

    local any=0
    for gov_file in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [[ -f "$gov_file" ]] || continue
        any=1

        # Check if requested governor is supported on this CPU
        local avail_file dir cpu_name
        dir=$(dirname "$gov_file")
        cpu_name=$(basename "$dir")
        avail_file="$dir/scaling_available_governors"

        if [[ -f "$avail_file" ]] && ! grep -qw "$target_gov" "$avail_file"; then
            echo "  WARNING: $cpu_name does not list '$target_gov' in scaling_available_governors"
            continue
        fi

        echo "$target_gov" > "$gov_file" 2>/dev/null || {
            echo "  ERROR: failed to set $cpu_name to '$target_gov' (permission or driver issue?)"
            continue
        }

        echo "  $cpu_name -> $target_gov"
    done

    if [[ $any -eq 0 ]]; then
        echo "ERROR: No cpufreq scaling_governor files found. Is cpufreq supported on this system?"
        exit 1
    fi

    echo "Done."
}

# ---- main ----

if [[ $# -eq 0 ]]; then
    print_usage
    exit 1
fi

ACTION=""
while getopts ":psbocgh" opt; do
    case "$opt" in
        p) ACTION="performance" ;;
        s) ACTION="powersave" ;;
        b) ACTION="schedutil" ;;
        o) ACTION="ondemand" ;;
        c) ACTION="conservative" ;;
        g) ACTION="show" ;;
        h) print_usage; exit 0 ;;
        \?) echo "Unknown option: -$OPTARG" >&2; print_usage; exit 1 ;;
    esac
done

if [[ -z "${ACTION}" ]]; then
    print_usage
    exit 1
fi

if [[ "$ACTION" == "show" ]]; then
    # showing governors does not require root
    show_governors
    exit 0
fi

# Need root to write to sysfs
require_root "$@"

set_governor_all "$ACTION"
show_governors
