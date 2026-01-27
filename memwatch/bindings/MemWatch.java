// Java/JNI binding for memwatch
// Place in: java/src/main/java/com/memwatch/MemWatch.java

package com.memwatch;

import java.nio.ByteBuffer;
import java.util.*;

/**
 * ChangeEvent - unified event across all languages
 */
public class ChangeEvent {
    public int seq;
    public long timestamp_ns;
    public int adapter_id;
    public int region_id;
    public String variable_name;
    
    public static class Location {
        public String file;
        public String function;
        public int line;
        public long fault_ip;
    }
    
    public Location where;
    public byte[] old_preview;
    public byte[] new_preview;
    public byte[] old_value;
    public byte[] new_value;
    public String storage_key_old;
    public String storage_key_new;
    public Map<String, Object> metadata;
    
    public ChangeEvent() {
        this.where = new Location();
        this.metadata = new HashMap<>();
    }
}

/**
 * MemWatch - Unified memory watcher for Java
 * 
 * Example:
 *   MemWatch watcher = new MemWatch();
 *   byte[] data = "hello".getBytes();
 *   watcher.watch(data, "my_data");
 *   watcher.setCallback(event -> System.out.println(event.variable_name + " changed"));
 *   data[0] = 72; // Triggers callback
 */
public class MemWatch {
    
    static {
        System.loadLibrary("memwatch_jni");
    }
    
    private native int nativeInit();
    private native void nativeShutdown();
    private native int nativeWatch(long addr, int size, String name);
    private native boolean nativeUnwatch(int region_id);
    private native int nativeSetCallback(ChangeEventCallback callback);
    private native ChangeEvent[] nativeCheckChanges();
    private native MemWatchStats nativeGetStats();
    
    @FunctionalInterface
    public interface ChangeEventCallback {
        void onChangeEvent(ChangeEvent event);
    }
    
    public static class MemWatchStats {
        public int num_tracked_regions;
        public int num_active_watchpoints;
        public long total_events;
        public long ring_write_count;
        public long ring_drop_count;
        public long storage_bytes_used;
    }
    
    private ChangeEventCallback callback;
    private Map<Integer, Object> trackedObjects;
    
    /**
     * Create a new MemWatch instance
     */
    public MemWatch() {
        this.trackedObjects = new HashMap<>();
        int result = nativeInit();
        if (result != 0) {
            throw new RuntimeException("Failed to initialize memwatch: " + result);
        }
    }
    
    /**
     * Watch a byte buffer for changes
     * @param buffer Buffer to watch
     * @param name Variable name (optional)
     * @return region_id
     */
    public int watch(byte[] buffer, String name) {
        if (buffer == null) {
            throw new IllegalArgumentException("Buffer cannot be null");
        }
        
        // Get memory address
        long addr = getBufferAddress(buffer);
        int region_id = nativeWatch(addr, buffer.length, name);
        
        if (region_id > 0) {
            trackedObjects.put(region_id, buffer);
        }
        
        return region_id;
    }
    
    /**
     * Watch a ByteBuffer for changes
     */
    public int watch(ByteBuffer buffer, String name) {
        if (buffer == null) {
            throw new IllegalArgumentException("Buffer cannot be null");
        }
        
        if (!buffer.isDirect()) {
            throw new IllegalArgumentException("ByteBuffer must be direct (off-heap)");
        }
        
        long addr = getDirectBufferAddress(buffer);
        int region_id = nativeWatch(addr, buffer.remaining(), name);
        
        if (region_id > 0) {
            trackedObjects.put(region_id, buffer);
        }
        
        return region_id;
    }
    
    /**
     * Stop watching a region
     */
    public boolean unwatch(int region_id) {
        boolean result = nativeUnwatch(region_id);
        if (result) {
            trackedObjects.remove(region_id);
        }
        return result;
    }
    
    /**
     * Set callback for change events
     */
    public void setCallback(ChangeEventCallback callback) {
        this.callback = callback;
        nativeSetCallback(callback);
    }
    
    /**
     * Synchronously check for changes (polling mode)
     */
    public List<ChangeEvent> checkChanges() {
        ChangeEvent[] events = nativeCheckChanges();
        return events != null ? Arrays.asList(events) : new ArrayList<>();
    }
    
    /**
     * Get statistics
     */
    public MemWatchStats getStats() {
        return nativeGetStats();
    }
    
    /**
     * Clean shutdown
     */
    public void shutdown() {
        nativeShutdown();
    }
    
    /**
     * Get memory address of a byte array (JNI helper)
     */
    private native long getBufferAddress(byte[] buffer);
    
    /**
     * Get memory address of a ByteBuffer (JNI helper)
     */
    private native long getDirectBufferAddress(ByteBuffer buffer);
}
