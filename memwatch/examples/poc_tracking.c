#define _GNU_SOURCE
#include "memwatch_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

int main() {
    // Initialize tracking
    if (tracker_init("test_poc.db", true, false, false, "both") < 0) {
        fprintf(stderr, "❌ Tracker init failed\n");
        return 1;
    }

    printf("✅ Tracker initialized\n");

    // Allocate page-aligned memory for tracking
    // On Linux, page size is usually 4096
    size_t pagesize = getpagesize();
    printf("Page size: %zu bytes\n", pagesize);

    int *x = memalign(pagesize, pagesize);
    if (!x) {
        perror("memalign");
        return 1;
    }

    memset(x, 0, pagesize);
    *x = 42;

    printf("Allocated page at %p, size %zu\n", (void*)x, pagesize);
    printf("Initial: *x=%d\n", *x);

    // Watch the entire page
    tracker_watch((uint64_t)x, pagesize, "x_page");

    printf("Watching page...\n");

    // Multiple writes
    *x = 100;
    printf("After write 1: *x=%d\n", *x);

    *x = 200;
    printf("After write 2: *x=%d\n", *x);

    x[10] = 999;
    printf("After offset write: x[10]=%d\n", x[10]);

    *x = 300;
    printf("After write 3: *x=%d\n", *x);

    tracker_close();

    int events = tracker_get_event_count();
    printf("\n✅ Done! Events recorded: %d\n", events);

    free(x);
    return 0;
}
