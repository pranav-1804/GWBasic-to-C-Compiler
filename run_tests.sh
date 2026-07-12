#!/bin/bash

# 1. Quick sanity check: Does the folder even exist?
if [ ! -d "test_BAS" ]; then
    echo "ERROR: Directory 'test_BAS' not found in the current folder."
    echo "Current directory is: $(pwd)"
    exit 1
fi

echo "Starting test run..."

# 2. Loop explicitly through both uppercase and lowercase extensions
for file in test_BAS/*.BAS test_BAS/*.bas; do

    # If no files match the pattern, bash returns the literal string.
    # This line safely skips it if the file doesn't actually exist.
    [ -f "$file" ] || continue

    # Extract the base name without path and extension (e.g., TEST1)
    filename=$(basename "$file")
    basename="${filename%.*}"

    echo "========================================"
    echo "Processing: $basename"
    echo "========================================"

    # 3. Run the Java GWBasic translator
    java -cp target/classes de.hft_stuttgart.cpl.GWBasic "$file"

    # Check if Java step succeeded
    if [ $? -eq 0 ]; then
        # 4. Compile the generated C file
        gcc -Wall -o "target/$basename" "target/${basename}.c" -lm

        # Check if Compilation succeeded
        if [ $? -eq 0 ]; then
            # 5. Execute the compiled program
            echo "--- Execution Output ---"
            "./target/$basename"
            echo ""
        else
            echo "ERROR: Compilation failed for $basename"
        fi
    else
        echo "ERROR: Java translation failed for $basename"
    fi
done

echo "Done!"