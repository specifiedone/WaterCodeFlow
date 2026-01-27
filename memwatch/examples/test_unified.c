/**
 * memwatch - C example with unified API
 * Same API and behavior as all other language bindings
 */

#include <memwatch_unified.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int event_count = 0;

void on_change(const memwatch_change_event_t *event, void *user_ctx) {
    event_count++;
    printf("  → Change detected: %s\n", event->variable_name);
    printf("    - Where: %s:%s:%u\n", event->file, event->function, event->line);
    printf("    - Old (first 8): ");
    for (int i = 0; i < 8 && i < event->old_preview_size; i++) {
        printf("%02x ", event->old_preview[i]);
    }
    printf("\n");
    printf("    - New (first 8): ");
    for (int i = 0; i < 8 && i < event->new_preview_size; i++) {
        printf("%02x ", event->new_preview[i]);
    }
    printf("\n");
}

int main() {
    printf("MemWatch - C Unified API Example\n");
    printf("==================================================\n");
    
    // Initialize
    if (memwatch_init() != 0) {
        fprintf(stderr, "Failed to initialize memwatch\n");
        return 1;
    }
    printf("✓ memwatch_init() succeeded\n");
    
    // Create buffer
    uint8_t data[14] = "Hello, World!";
    printf("✓ Created buffer: %s\n", (char*)data);
    
    // Watch it
    uint32_t region_id = memwatch_watch((uint64_t)data, sizeof(data), "data", NULL);
    if (region_id == 0) {
        fprintf(stderr, "Failed to watch buffer\n");
        return 1;
    }
    printf("✓ Started watching region %u\n", region_id);
    
    // Set callback
    memwatch_set_callback(on_change, NULL);
    printf("✓ Callback registered\n");
    
    // Modify data
    printf("\nModifying data...\n");
    data[0] = 'J';  // H -> J
    usleep(100000);
    
    memcpy(data + 7, "Cogram", 6);  // World -> Cogram
    usleep(100000);
    
    // Check stats
    memwatch_stats_t stats = {0};
    memwatch_get_stats(&stats);
    printf("\nStats:\n");
    printf("  - Tracked regions: %u\n", stats.num_tracked_regions);
    printf("  - Total events: %u\n", stats.total_events);
    printf("  - Event count: %d\n", event_count);
    
    // Cleanup
    memwatch_unwatch(region_id);
    printf("\n✓ Stopped watching region %u\n", region_id);
    
    memwatch_shutdown();
    printf("\n✓ memwatch_shutdown() completed\n");
    
    // Verify
    if (event_count > 0) {
        printf("\n✓ SUCCESS: Detected %d change event(s)\n", event_count);
        return 0;
    } else {
        printf("\n✗ FAILURE: No change events detected\n");
        return 1;
    }
}
