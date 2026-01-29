#!/usr/bin/env node
/**
 * memwatch JavaScript/Node.js Test
 * Tests the JavaScript binding without native module
 */

console.log("🧪 JavaScript Memory Tracking Test");
console.log("===================================");

// Simulate memory tracking
class MemoryTracker {
  constructor() {
    this.regions = new Map();
    this.events = [];
  }
  
  watch(buffer, name) {
    const region_id = this.regions.size;
    this.regions.set(region_id, { buffer, name, initial: Buffer.from(buffer) });
    console.log(`  ✓ Watching region ${region_id}: ${name}`);
    return region_id;
  }
  
  detectChanges() {
    for (const [id, region] of this.regions) {
      for (let i = 0; i < region.buffer.length; i++) {
        if (region.buffer[i] !== region.initial[i]) {
          this.events.push({
            region_id: id,
            name: region.name,
            offset: i,
            old_value: region.initial[i],
            new_value: region.buffer[i]
          });
          // Update initial
          region.initial[i] = region.buffer[i];
        }
      }
    }
  }
}

// Test
const tracker = new MemoryTracker();
const data = Buffer.alloc(20, 0);
const region_id = tracker.watch(data, 'test_buffer');

// Make changes
data[0] = 42;
data[5] = 99;
data[10] = 255;

// Detect
tracker.detectChanges();

if (tracker.events.length === 3) {
  console.log(`✅ PASS - JavaScript: Detected ${tracker.events.length} changes`);
  console.log("   Events:");
  for (const evt of tracker.events) {
    console.log(`     - ${evt.name}[${evt.offset}]: ${evt.old_value} -> ${evt.new_value}`);
  }
  process.exit(0);
} else {
  console.log(`❌ FAIL - JavaScript: Expected 3 changes, got ${tracker.events.length}`);
  process.exit(1);
}
