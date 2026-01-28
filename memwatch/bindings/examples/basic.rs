// Rust example demonstrating memwatch with max_value_bytes parameter
// Usage: cargo run --example basic

use memwatch::{MemWatch, ChangeEvent};
use std::time::Duration;
use std::thread;

fn main() -> Result<(), String> {
    println!("MemWatch Rust Example - max_value_bytes Support");
    println!("================================================\n");

    // Initialize memwatch
    let watcher = MemWatch::new()?;
    println!("✓ MemWatch initialized");

    // Example 1: No value storage (max_value_bytes=0)
    println!("\n1️⃣  Watching with max_value_bytes=0 (no values)");
    let mut buf1 = vec![1u8, 2, 3, 4, 5];
    let region_id_1 = watcher.watch_vec_with_max_value_bytes(&buf1, "no_values", 0)?;
    println!("   → Watching: no_values ({}), region_id={}", buf1.len(), region_id_1);
    
    buf1[0] = 99;
    thread::sleep(Duration::from_millis(100));
    let events = watcher.check_changes();
    println!("   → Events: {} (values: {})", events.len(), 
        events.iter().map(|e| format!("old:{:?}", e.old_preview)).collect::<Vec<_>>().join(", "));

    // Example 2: Limited value storage (max_value_bytes=2)
    println!("\n2️⃣  Watching with max_value_bytes=2 (limited)");
    let mut buf2 = vec![10u8, 20, 30, 40, 50, 60];
    let region_id_2 = watcher.watch_vec_with_max_value_bytes(&buf2, "limited_values", 2)?;
    println!("   → Watching: limited_values ({}), region_id={}", buf2.len(), region_id_2);
    
    buf2[3] = 99;
    thread::sleep(Duration::from_millis(100));
    let events = watcher.check_changes();
    println!("   → Events: {}, stored {} bytes max", events.len(), 2);

    // Example 3: Full value storage (max_value_bytes=-1)
    println!("\n3️⃣  Watching with max_value_bytes=-1 (full storage)");
    let mut buf3 = vec![100u8, 200, 50, 75, 25];
    let region_id_3 = watcher.watch_vec_with_max_value_bytes(&buf3, "full_values", -1)?;
    println!("   → Watching: full_values ({}), region_id={}", buf3.len(), region_id_3);
    
    buf3[2] = 125;
    thread::sleep(Duration::from_millis(100));
    let events = watcher.check_changes();
    println!("   → Events: {} (full {} bytes stored)", events.len(), buf3.len());

    // Get statistics
    let stats = watcher.get_stats();
    println!("\n📊 Statistics:");
    println!("   - Tracked regions: {}", stats.num_tracked_regions);
    println!("   - Total events: {}", stats.total_events);
    println!("   - Storage used: {} bytes", stats.storage_bytes_used);

    // Cleanup
    watcher.unwatch(region_id_1);
    watcher.unwatch(region_id_2);
    watcher.unwatch(region_id_3);
    println!("\n✓ All regions unwatched");
    println!("✓ SUCCESS: Rust memwatch example completed");

    Ok(())
}
