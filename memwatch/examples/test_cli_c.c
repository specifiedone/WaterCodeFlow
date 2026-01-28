#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Test program for memwatch CLI
 * Demonstrates memory modifications in C
 */

int main(int argc, char *argv[]) {
    printf("🚀 C Test Program - Memory Tracking\n");
    printf("Making changes to variables...\n\n");
    
    /* Test 1: Integer modifications */
    int counter = 0;
    for (int i = 0; i < 5; i++) {
        counter += i;
        printf("[C] counter = %d\n", counter);
        sleep(1);
    }
    
    /* Test 2: String buffer modifications */
    char buffer[256] = "Hello";
    printf("\n[C] buffer = \"%s\"\n", buffer);
    
    strcat(buffer, " World");
    printf("[C] buffer = \"%s\"\n", buffer);
    sleep(1);
    
    strcat(buffer, "!");
    printf("[C] buffer = \"%s\"\n", buffer);
    sleep(1);
    
    /* Test 3: Array modifications */
    int arr[5] = {1, 2, 3, 4, 5};
    printf("\n[C] Array modifications:\n");
    for (int i = 0; i < 5; i++) {
        arr[i] *= 2;
        printf("[C] arr[%d] = %d\n", i, arr[i]);
        sleep(1);
    }
    
    printf("\n✅ C test complete\n");
    return 0;
}
