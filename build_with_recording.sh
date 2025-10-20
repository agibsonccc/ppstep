#!/bin/bash

# Build script for ppstep with recording functionality

cd /home/agibsonccc/Documents/GitHub/ppstep

# Clean previous build
make clean 2>/dev/null

# Build
echo "Building ppstep with recording functionality..."
cmake . && make

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Testing the recording functionality:"
    echo "====================================="
    echo ""
    echo "Example usage:"
    echo "  ./ppstep test_macros.c"
    echo ""
    echo "Then in the ppstep prompt:"
    echo "  pp> record output.txt"
    echo "  pp> step 100"
    echo "  pp> stop-record"
    echo ""
    echo "Or to trace a specific macro:"
    echo "  pp> break call STRINGIFY"
    echo "  pp> record stringify_trace.txt"  
    echo "  pp> continue"
    echo "  pp> step 50"
    echo "  pp> stop-record"
else
    echo "Build failed. Please check the error messages above."
fi