/**
 * memwatch - JavaScript/Node.js example with unified API
 * Same API and behavior as all other language bindings
 */

const memwatch = require('../bindings/memwatch');

async function main() {
    console.log('MemWatch - JavaScript/Node.js Unified API Example');
    console.log('='.repeat(50));
    
    // Create watcher
    const watcher = memwatch.create();
    console.log('✓ MemWatch created');
    
    // Create a buffer to watch
    const data = Buffer.from('Hello, World!');
    console.log(`✓ Created buffer: ${data.toString()}`);
    
    // Watch it
    const region_id = watcher.watch(data, 'data');
    console.log(`✓ Started watching region ${region_id}`);
    
    // Set up callback
    let events = [];
    watcher.on('change', (event) => {
        console.log(`  → Change detected: ${event.variable_name}`);
        console.log(`    - Where: ${JSON.stringify(event.where)}`);
        if (event.old_preview) console.log(`    - Old: ${event.old_preview.toString('hex').substr(0, 16)}`);
        if (event.new_preview) console.log(`    - New: ${event.new_preview.toString('hex').substr(0, 16)}`);
        events.push(event);
    });
    console.log('✓ Callback registered');
    
    // Modify data - should trigger callback
    console.log('\nModifying data...');
    data[0] = 74; // J (was H)
    await new Promise(r => setTimeout(r, 100));
    
    data.write('JavaScript', 7);
    await new Promise(r => setTimeout(r, 100));
    
    // Check stats
    const stats = watcher.get_stats();
    console.log(`\nStats:`);
    console.log(`  - Tracked regions: ${stats.num_tracked_regions}`);
    console.log(`  - Total events: ${events.length}`);
    
    // Cleanup
    watcher.unwatch(region_id);
    console.log(`\n✓ Stopped watching region ${region_id}`);
    
    // Verify events were captured
    if (events.length > 0) {
        console.log(`\n✓ SUCCESS: Detected ${events.length} change event(s)`);
        return 0;
    } else {
        console.log('\n✗ FAILURE: No change events detected');
        return 1;
    }
}

main().then(code => process.exit(code)).catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
