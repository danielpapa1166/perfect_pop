#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "======== Running script from directory: $SCRIPT_DIR ========"

echo "======== Generating chirp noise signal ========"
python3 "$SCRIPT_DIR/generate_chirp_noise.py"

if [ -d "$SCRIPT_DIR/build" ]; then
    echo "======== Removing build directory ========"
    rm -r "$SCRIPT_DIR/build"
fi

echo "======== Build simulation project ========" 
cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build"

echo "======== Running simulation ========"
"$SCRIPT_DIR/build/filter_sim"

echo "======== Update plots ========"
python3 "$SCRIPT_DIR/plot_filter_streams.py"
