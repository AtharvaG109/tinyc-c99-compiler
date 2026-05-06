#!/usr/bin/env bash
set -e

# Stage 0: gcc builds your compiler
gcc -std=c11 -O0 -Iinclude -o tinyc_s0 src/*.c

# Stage 1: your compiler builds itself
echo "Building Stage 1..."
for f in src/*.c; do
    name=$(basename $f .c)
    gcc -E -std=c11 -Iinclude $f | ./tinyc_s0 -o /tmp/${name}_s1.s -
done
gcc /tmp/*_s1.s -o tinyc_s1

# Stage 2: stage1 builds itself
echo "Building Stage 2..."
for f in src/*.c; do
    name=$(basename $f .c)
    gcc -E -std=c11 -Iinclude $f | ./tinyc_s1 -o /tmp/${name}_s2.s -
done
gcc /tmp/*_s2.s -o tinyc_s2

# Validation
if cmp -s tinyc_s1 tinyc_s2; then
    echo "✓ SELF-HOSTING VERIFIED — stage1 == stage2"
    exit 0
else
    echo "✗ SELF-HOSTING FAILED — stage1 != stage2"
    diff <(objdump -d tinyc_s1) <(objdump -d tinyc_s2) | head -40
    exit 1
fi
