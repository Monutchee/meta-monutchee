##
# @file auto_clean_error.py
#
# @brief This script will have you to auto clean the old built package when you pulled branch
#
# @section description_doxygen_example Description
# Run: python3 auto_clean_error.py
# 
##


import subprocess
import re
from collections import defaultdict

# Maximum retries for a single package
MAX_RETRY = 3

def run_bitbake():
    error_counts = defaultdict(int)  # Track retries for each package
    while True:
        # Run bitbake command
        print("\nRunning bitbake...")
        process = subprocess.Popen(["bitbake", "mncos-image-minimal"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        error_packages = set()
        
        output_lines = []
        for line in process.stdout:
            print(line, end="")  # Print the output for visibility
            output_lines.append(line)  # Store lines for post-processing
        
        # Wait for the process to finish
        process.wait()
        if process.returncode == 0:
            print("\nBuild succeeded! No errors detected.")
            break  # Exit the loop if no errors occur
        
        # Parse errors from output
        error_packages = parse_errors(output_lines)
        if not error_packages:
            print("\nNo error packages detected in output, exiting loop.")
            break  # Exit loop if no errors are detected

        print("\nDetected error packages:", error_packages)
        
        # Clean detected error packages
        for package in error_packages:
            error_counts[package] += 1
            if error_counts[package] > MAX_RETRY:
                print(f"\nUnresolved error for package {package}, tried {MAX_RETRY} times. Exiting.")
                return  # Terminate script if a package exceeds MAX_RETRY
            clean_package(package)

def parse_errors(output_lines):
    error_packages = set()
    # Look for lines that indicate failed tasks in the "Summary" section
    for line in output_lines:
        # Match the package name before the version and revision
        match = re.search(r"ERROR: ([\w\-]+)-\d[\d\.]+-[\w\-]+", line)
        if match:
            error_packages.add(match.group(1))  # Extract package name before the version
    return error_packages

def clean_package(package):
    print(f"Cleaning package: {package}")
    result = subprocess.run(["bitbake", "-c", "clean", package], text=True)
    if result.returncode == 0:
        print(f"Successfully cleaned {package}")
    else:
        print(f"Failed to clean {package}, please check manually.")

if __name__ == "__main__":
    run_bitbake()

