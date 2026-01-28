/**
 * Callback function for memwatch CLI (JavaScript/Node.js)
 * 
 * This function is called whenever a memory change is detected.
 * The function MUST be named 'main' for memwatch to invoke it.
 * 
 * Usage:
 *   memwatch run node script.js --user-func my_callback.js --user-func-lang javascript
 */

const fs = require('fs');
const path = require('path');

function main() {
    /**
     * Main callback - called on each memory change
     */
    
    console.log("🔔 [Callback] Memory change detected!");
    console.log("   This callback was triggered by memwatch CLI");
    console.log("   You can add custom logic here:");
    console.log("   - Log changes to database");
    console.log("   - Alert on specific patterns");
    console.log("   - Trigger other actions");
    console.log("   - Send to monitoring service");
    
    // Find the latest event file
    const tmpDir = '/tmp';
    const files = fs.readdirSync(tmpDir)
        .filter(f => f.startsWith('memwatch_event_') && f.endsWith('.json'))
        .map(f => ({
            name: f,
            path: path.join(tmpDir, f),
            time: fs.statSync(path.join(tmpDir, f)).mtimeMs
        }))
        .sort((a, b) => b.time - a.time);
    
    if (files.length > 0) {
        try {
            const eventData = JSON.parse(fs.readFileSync(files[0].path, 'utf8'));
            console.log("   Event data:", eventData);
        } catch (e) {
            console.error("   Error reading event:", e.message);
        }
    }
}

// Export for use as module
module.exports = { main };

// Run if called directly
if (require.main === module) {
    main();
}
