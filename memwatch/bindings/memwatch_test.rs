use std::collections::HashMap;

#[derive(Clone, Debug)]
struct MemoryEvent {
    name: String,
    offset: usize,
    old_value: u8,
    new_value: u8,
}

struct MemoryTracker {
    regions: HashMap<usize, Vec<u8>>,
    initial: HashMap<usize, Vec<u8>>,
    events: Vec<MemoryEvent>,
    region_count: usize,
}

impl MemoryTracker {
    fn new() -> Self {
        MemoryTracker {
            regions: HashMap::new(),
            initial: HashMap::new(),
            events: Vec::new(),
            region_count: 0,
        }
    }

    fn watch(&mut self, data: &[u8], name: &str) -> usize {
        let id = self.region_count;
        self.region_count += 1;

        self.regions.insert(id, data.to_vec());
        self.initial.insert(id, data.to_vec());

        println!("  ✓ Watching region {}: {}", id, name);
        id
    }

    fn detect_changes(&mut self) {
        for id in self.regions.keys().copied().collect::<Vec<_>>() {
            let region = self.regions[&id].clone();
            let mut init = self.initial[&id].clone();

            for i in 0..region.len() {
                if init[i] != region[i] {
                    self.events.push(MemoryEvent {
                        name: format!("region_{}", id),
                        offset: i,
                        old_value: init[i],
                        new_value: region[i],
                    });
                    init[i] = region[i];
                }
            }
            self.initial.insert(id, init);
        }
    }

    fn event_count(&self) -> usize {
        self.events.len()
    }
}

fn main() {
    println!("🧪 Rust Memory Tracking Test");
    println!("============================");

    let mut tracker = MemoryTracker::new();
    let mut data = vec![0u8; 20];

    let region_id = tracker.watch(&data, "test_buffer");
    let _ = region_id;

    data[0] = 42;
    data[5] = 99;
    data[10] = 255;

    // Update the tracker's regions
    tracker.regions.insert(0, data.clone());
    
    tracker.detect_changes();

    if tracker.event_count() == 3 {
        println!("✅ PASS - Rust: Detected {} changes", tracker.event_count());
        println!("   Events:");
        for evt in &tracker.events {
            println!("     - {}[{}]: {} -> {}", evt.name, evt.offset, evt.old_value, evt.new_value);
        }
    } else {
        println!("❌ FAIL - Rust: Expected 3 changes, got {}", tracker.event_count());
    }
}
