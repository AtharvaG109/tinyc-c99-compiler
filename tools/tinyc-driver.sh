#!/bin/bash
# tinyc-driver.sh — Wraps gcc -E | tinyc for full compilation
#
# Usage:
#   tinyc-driver.sh [options] <input.c> [-o output]
#
# This script:
#   1. Preprocesses with gcc -E -std=c99
#   2. Compiles with tinyc → .s assembly
#   3. Assembles + links with gcc → executable

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TINYC="${TINYC:-${SCRIPT_DIR}/../build/tinyc}"

# Parse arguments
OUTPUT=""
INPUT=""
EXTRA_FLAGS=()
DUMP_TOKENS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        -o)
            OUTPUT="$2"
            shift 2
            ;;
        --dump-tokens)
            DUMP_TOKENS=1
            shift
            ;;
        -*)
            EXTRA_FLAGS+=("$1")
            shift
            ;;
        *)
            INPUT="$1"
            shift
            ;;
    esac
done

if [[ -z "$INPUT" ]]; then
    echo "Usage: tinyc-driver.sh [options] <input.c> [-o output]" >&2
    exit 1
fi

if [[ ! -f "$INPUT" ]]; then
    echo "Error: file not found: $INPUT" >&2
    exit 1
fi

if [[ "$DUMP_TOKENS" -eq 1 ]]; then
    # Lex-only mode: preprocess and dump tokens
    gcc -E -std=c99 "${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"}" "$INPUT" | "$TINYC" --dump-tokens -
    exit 0
fi

# Default output name
if [[ -z "$OUTPUT" ]]; then
    OUTPUT="${INPUT%.c}"
fi

# Full compilation pipeline
TMPASM=$(mktemp /tmp/tinyc_XXXXXX.s)
trap "rm -f '$TMPASM'" EXIT

gcc -E -std=c99 "${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"}" "$INPUT" | "$TINYC" -o "$TMPASM" -
gcc -o "$OUTPUT" "$TMPASM" "${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"}"

echo "Compiled: $INPUT → $OUTPUT"
