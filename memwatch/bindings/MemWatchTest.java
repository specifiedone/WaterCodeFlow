/**
 * MemWatch Java Test
 * Tests basic Java memory tracking simulation
 */

import java.util.*;

public class MemWatchTest {
    static class MemoryEvent {
        String name;
        int offset;
        int oldValue;
        int newValue;
        
        MemoryEvent(String name, int offset, int oldValue, int newValue) {
            this.name = name;
            this.offset = offset;
            this.oldValue = oldValue;
            this.newValue = newValue;
        }
    }
    
    static class MemoryTracker {
        Map<Integer, byte[]> regions = new HashMap<>();
        Map<Integer, byte[]> initial = new HashMap<>();
        List<MemoryEvent> events = new ArrayList<>();
        int regionCounter = 0;
        
        int watch(byte[] data, String name) {
            int id = regionCounter++;
            byte[] dataCopy = new byte[data.length];
            System.arraycopy(data, 0, dataCopy, 0, data.length);
            
            byte[] initialCopy = new byte[data.length];
            System.arraycopy(data, 0, initialCopy, 0, data.length);
            
            regions.put(id, dataCopy);
            initial.put(id, initialCopy);
            System.out.println("  ✓ Watching region " + id + ": " + name);
            return id;
        }
        
        void detectChanges() {
            for (Integer id : regions.keySet()) {
                byte[] region = regions.get(id);
                byte[] init = initial.get(id);
                for (int i = 0; i < region.length; i++) {
                    if (init[i] != region[i]) {
                        events.add(new MemoryEvent("region_" + id, i, init[i] & 0xFF, region[i] & 0xFF));
                        init[i] = region[i];
                    }
                }
            }
        }
    }
    
    public static void main(String[] args) {
        System.out.println("🧪 Java Memory Tracking Test");
        System.out.println("============================");
        
        MemoryTracker tracker = new MemoryTracker();
        byte[] data = new byte[20];
        Arrays.fill(data, (byte)0);
        
        int regionId = tracker.watch(data, "test_buffer");
        
        // Modify the array
        data[0] = 42;
        data[5] = 99;
        data[10] = (byte)255;
        
        // Update region to reflect changes
        tracker.regions.put(regionId, data);
        
        tracker.detectChanges();
        
        if (tracker.events.size() == 3) {
            System.out.println("✅ PASS - Java: Detected " + tracker.events.size() + " changes");
            System.out.println("   Events:");
            for (MemoryEvent evt : tracker.events) {
                System.out.println("     - " + evt.name + "[" + evt.offset + "]: " + evt.oldValue + " -> " + evt.newValue);
            }
            System.exit(0);
        } else {
            System.out.println("❌ FAIL - Java: Expected 3 changes, got " + tracker.events.size());
            System.exit(1);
        }
    }
}
