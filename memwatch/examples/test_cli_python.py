#!/usr/bin/env python3
"""
Test script for memwatch CLI - demonstrates tracking Python code
"""

import time
import sys

def modify_variables():
    """Function that modifies variables to be tracked"""
    x = 100
    y = "hello"
    z = [1, 2, 3]
    
    print(f"Initial: x={x}, y={y}, z={z}")
    
    # Modify variables
    for i in range(5):
        x = x + i
        y = y + str(i)
        z.append(i * 10)
        print(f"Step {i}: x={x}, y={y}, z={z}")
        time.sleep(0.1)
    
    return x, y, z

def main():
    """Main function"""
    print("🚀 Starting test script for memwatch CLI")
    print("Memory changes should be tracked...")
    print("")
    
    # Run the main function
    x, y, z = modify_variables()
    
    print("")
    print("✅ Test complete")
    print(f"Final values: x={x}, y={y}, z={z}")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
