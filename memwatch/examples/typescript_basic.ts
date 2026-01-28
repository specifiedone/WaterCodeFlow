#!/usr/bin/env node

/**
 * TypeScript/Node.js example demonstrating memwatch with max_value_bytes parameter
 * Usage: npx ts-node examples/basic.ts
 */

import MemWatch, { ChangeEvent } from '../build/memwatch';

async function sleep(ms: number): Promise<void> {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function main() {
    console.log('MemWatch TypeScript Example - max_value_bytes Support');
    console.log('====================================================\n');

    const watcher = new MemWatch();
    console.log('✓ MemWatch initialized');

    // Example 1: No value storage (max_value_bytes=0)
    console.log('\n1️⃣  Watching with max_value_bytes=0 (no values)');
    const buf1 = Buffer.from([1, 2, 3, 4, 5]);
    const region_id_1 = watcher.watch(buf1, 'no_values', 0);
    console.log(`   → Watching: no_values (${buf1.length}), region_id=${region_id_1}`);

    buf1[0] = 99;
    await sleep(100);
    const events1 = watcher.check_changes();
    console.log(`   → Events: ${events1.length} (no values stored)`);

    // Example 2: Limited value storage (max_value_bytes=2)
    console.log('\n2️⃣  Watching with max_value_bytes=2 (limited)');
    const buf2 = Buffer.from([10, 20, 30, 40, 50, 60]);
    const region_id_2 = watcher.watch(buf2, 'limited_values', 2);
    console.log(`   → Watching: limited_values (${buf2.length}), region_id=${region_id_2}`);

    buf2[3] = 99;
    await sleep(100);
    const events2 = watcher.check_changes();
    console.log(`   → Events: ${events2.length}, stored max 2 bytes`);

    // Example 3: Full value storage (max_value_bytes=-1)
    console.log('\n3️⃣  Watching with max_value_bytes=-1 (full storage)');
    const buf3 = Buffer.from([100, 200, 50, 75, 25]);
    const region_id_3 = watcher.watch(buf3, 'full_values', -1);
    console.log(`   → Watching: full_values (${buf3.length}), region_id=${region_id_3}`);

    buf3[2] = 125;
    await sleep(100);
    const events3 = watcher.check_changes();
    console.log(`   → Events: ${events3.length} (full ${buf3.length} bytes stored)`);

    // Get statistics
    const stats = watcher.get_stats();
    console.log('\n📊 Statistics:');
    console.log(`   - Tracked regions: ${stats.num_tracked_regions}`);
    console.log(`   - Total events: ${stats.total_events}`);
    console.log(`   - Storage used: ${stats.storage_bytes_used} bytes`);

    // Cleanup
    watcher.unwatch(region_id_1);
    watcher.unwatch(region_id_2);
    watcher.unwatch(region_id_3);
    console.log('\n✓ All regions unwatched');

    watcher.shutdown();
    console.log('✓ SUCCESS: TypeScript memwatch example completed');
}

main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
