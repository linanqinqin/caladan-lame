#!/usr/bin/env python3
"""
parse_bundle.py - LAME bundle log parser

This program parses LAME bundle logs from stdin and validates bundle consistency
and lifecycle per kthread.

Usage: ./hello_world | python3 parse_bundle.py
"""

import sys
import re
from collections import defaultdict
from typing import Dict, List, Tuple, Optional

class BundleInfo:
    def __init__(self):
        self.size = -1
        self.used = -1
        self.active = -1
        self.enabled = -1
        self.bundle_str = None
        self.uthreads = []
    
    def __str__(self):
        return f"Bundle(size={self.size}, used={self.used}, active={self.active}, enabled={self.enabled}, bundle={self.bundle_str})"

class KthreadBundle:
    def __init__(self, kthread_id: int):
        self.kthread_id = kthread_id
        self.bundles: List[BundleInfo] = []
        self.validation_errors = False
        self.entry_errors = []
    
    def add_bundle(self, bundle: BundleInfo):
        self.bundles.append(bundle)
    
    def get_last_bundle(self) -> Optional[BundleInfo]:
        return self.bundles[-1] if self.bundles else None

def parse_bundle_string(bundle_str: str) -> List[str]:
    """Parse bundle string to extract uthread addresses."""
    if not bundle_str:
        return []
    
    # Remove opening '<' if present
    if bundle_str.startswith('<'):
        bundle_str = bundle_str[1:]
    
    # Split by comma and clean up
    uthreads = []
    for item in bundle_str.split(','):
        item = item.strip()
        if item == "(nil)" or item.startswith("0x"):
            uthreads.append(item)
    
    return uthreads

def parse_bundle_line(line: str) -> Optional[Tuple[int, BundleInfo]]:
    """Parse a LAME bundle line and return (kthread_id, bundle_info)."""
    # Look for LAME bundle pattern
    if "[LAME][BUNDLE]" not in line:
        return None
    
    bundle = BundleInfo()
    
    # Extract kthread ID
    kthread_match = re.search(r'\[kthread:(\d+)\]', line)
    if not kthread_match:
        return None
    kthread_id = int(kthread_match.group(1))
    
    # Extract other fields using regex
    size_match = re.search(r'\[size:(\d+)\]', line)
    if size_match:
        bundle.size = int(size_match.group(1))
    
    used_match = re.search(r'\[used:(\d+)\]', line)
    if used_match:
        bundle.used = int(used_match.group(1))
    
    active_match = re.search(r'\[active:(\d+)\]', line)
    if active_match:
        bundle.active = int(active_match.group(1))
    
    enabled_match = re.search(r'\[enabled:(\d+)\]', line)
    if enabled_match:
        bundle.enabled = int(enabled_match.group(1))
    
    # Extract bundle string
    bundle_match = re.search(r'\[bundle:([^]]+)\]', line)
    if bundle_match:
        bundle.bundle_str = bundle_match.group(1)
        bundle.uthreads = parse_bundle_string(bundle.bundle_str)
    
    # Validate that we have the required fields
    if bundle.size < 0 or bundle.used < 0 or bundle.bundle_str is None:
        return None
    
    return kthread_id, bundle

def validate_bundle(bundle: BundleInfo, line_num: int) -> bool:
    """Validate bundle consistency."""
    valid = True
    
    # Check 1: used <= size
    if bundle.used > bundle.size:
        print(f"ERROR line {line_num}: used ({bundle.used}) > size ({bundle.size})")
        valid = False
    
    # Check 2: bundle field has exactly "used" many non-nil uthreads
    non_nil_count = sum(1 for uthread in bundle.uthreads if uthread != "(nil)")
    if non_nil_count != bundle.used:
        print(f"ERROR line {line_num}: bundle has {non_nil_count} non-nil uthreads but used={bundle.used}")
        valid = False
    
    return valid

def validate_kthread_lifecycle(kthread: KthreadBundle) -> bool:
    """Validate kthread bundle lifecycle."""
    valid = True
    
    if len(kthread.bundles) < 2:
        return True  # Need at least 2 entries to check growth
    
    # Check 1: bundle never grows for more than two consecutive times
    consecutive_growth = 0
    for i in range(1, len(kthread.bundles)):
        if kthread.bundles[i].used > kthread.bundles[i-1].used:
            consecutive_growth += 1
            if consecutive_growth > 2:
                print(f"ERROR kthread {kthread.kthread_id}: bundle grew for {consecutive_growth} consecutive times (entry {i + 1})")
                valid = False
                kthread.entry_errors.append(i)
        else:
            consecutive_growth = 0
    
    # Check 2: bundle ends up empty
    if kthread.bundles:
        last_bundle = kthread.bundles[-1]
        if last_bundle.used != 0:
            print(f"ERROR kthread {kthread.kthread_id}: bundle does not end empty (used={last_bundle.used} in last entry)")
            valid = False
            kthread.entry_errors.append(len(kthread.bundles) - 1)
    
    return valid

def print_kthread_summary(kthread: KthreadBundle):
    """Print kthread bundle summary."""
    print(f"\n=== KTHREAD {kthread.kthread_id} ===")
    print(f"Total Bundle Entries: {len(kthread.bundles)}")
    print(f"Validation Status: {'FAILED' if kthread.validation_errors else 'PASSED'}")
    
    if kthread.validation_errors and kthread.entry_errors:
        print("Bundle History with Errors:")
        for i in kthread.entry_errors:
            if i < len(kthread.bundles):
                bundle = kthread.bundles[i]
                print(f"  {i + 1:2d}: size={bundle.size} used={bundle.used} active={bundle.active} enabled={bundle.enabled} bundle={bundle.bundle_str} [ERROR]")
    print("==================")

def main():
    print("LAME Bundle Log Parser - Starting")
    
    # Dictionary to store kthread bundles
    kthreads: Dict[int, KthreadBundle] = {}
    line_num = 0
    
    print("LAME Bundle Log Parser")
    print("Reading from stdin...\n")
    
    # Read lines from stdin
    for line in sys.stdin:
        line_num += 1
        
        # Look for LAME bundle lines
        if "[LAME][BUNDLE]" in line:
            result = parse_bundle_line(line)
            if result:
                kthread_id, bundle = result
                
                # Get or create kthread bundle
                if kthread_id not in kthreads:
                    kthreads[kthread_id] = KthreadBundle(kthread_id)
                kthread = kthreads[kthread_id]
                
                # Validate bundle consistency
                bundle_valid = validate_bundle(bundle, line_num)
                if not bundle_valid:
                    print(f"Bundle validation failed at line {line_num}")
                
                # Add to kthread history
                kthread.add_bundle(bundle)
                
                # Mark error if validation failed
                if not bundle_valid:
                    kthread.entry_errors.append(len(kthread.bundles) - 1)
    
    # Validate kthread lifecycles
    print("\n=== BUNDLE LIFECYCLE VALIDATION ===")
    for kthread in kthreads.values():
        if not validate_kthread_lifecycle(kthread):
            kthread.validation_errors = True
    
    # Print summaries
    print("\n=== BUNDLE SUMMARY ===")
    print(f"Total KTHREADs: {len(kthreads)}")
    
    for kthread in sorted(kthreads.values(), key=lambda k: k.kthread_id):
        print_kthread_summary(kthread)

if __name__ == "__main__":
    main()
