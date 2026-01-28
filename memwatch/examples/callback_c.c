#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Callback function for memwatch CLI
 * 
 * This function is called whenever a memory change is detected.
 * The function MUST be named 'main' for memwatch to invoke it.
 * 
 * Usage:
 *   memwatch run ./program --user-func my_callback.c --user-func-lang c
 *   (compile as: gcc -c my_callback.c && gcc my_callback.o -o callback)
 */

typedef struct {
    const char *variable;
    const char *old_value;
    const char *new_value;
    unsigned int region_id;
    unsigned long timestamp;
} MemwatchEvent;

/**
 * Main callback - called on each memory change
 */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("🔔 [Callback] Memory change detected!\n");
    printf("   This callback was triggered by memwatch CLI\n");
    printf("   You can add custom logic here:\n");
    printf("   - Log changes to file\n");
    printf("   - Alert on suspicious activity\n");
    printf("   - Analyze patterns\n");
    printf("   - Send to monitoring system\n");
    
    return 0;
}
