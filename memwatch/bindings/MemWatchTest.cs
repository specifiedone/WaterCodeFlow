using System;
using System.Collections.Generic;

class MemWatchTest {
    class MemoryEvent {
        public string Name { get; set; }
        public int Offset { get; set; }
        public int OldValue { get; set; }
        public int NewValue { get; set; }
        
        public MemoryEvent(string name, int offset, int oldValue, int newValue) {
            Name = name;
            Offset = offset;
            OldValue = oldValue;
            NewValue = newValue;
        }
    }
    
    class MemoryTracker {
        Dictionary<int, byte[]> regions = new Dictionary<int, byte[]>();
        Dictionary<int, byte[]> initial = new Dictionary<int, byte[]>();
        List<MemoryEvent> events = new List<MemoryEvent>();
        int regionCounter = 0;
        
        public int Watch(byte[] data, string name) {
            int id = regionCounter++;
            regions[id] = (byte[])data.Clone();
            initial[id] = (byte[])data.Clone();
            Console.WriteLine($"  ✓ Watching region {id}: {name}");
            return id;
        }
        
        public void DetectChanges() {
            foreach (var kvp in regions) {
                int id = kvp.Key;
                byte[] region = kvp.Value;
                byte[] init = initial[id];
                
                for (int i = 0; i < region.Length; i++) {
                    if (init[i] != region[i]) {
                        events.Add(new MemoryEvent($"region_{id}", i, init[i], region[i]));
                        init[i] = region[i];
                    }
                }
            }
        }
        
        public int EventCount => events.Count;
        public List<MemoryEvent> Events => events;
    }
    
    static void Main() {
        Console.WriteLine("🧪 C# Memory Tracking Test");
        Console.WriteLine("==========================");
        
        MemoryTracker tracker = new MemoryTracker();
        byte[] data = new byte[20];
        Array.Fill(data, (byte)0);
        
        int regionId = tracker.Watch(data, "test_buffer");
        
        data[0] = 42;
        data[5] = 99;
        data[10] = 255;
        
        tracker.DetectChanges();
        
        if (tracker.EventCount == 3) {
            Console.WriteLine($"✅ PASS - C#: Detected {tracker.EventCount} changes");
            Console.WriteLine("   Events:");
            foreach (var evt in tracker.Events) {
                Console.WriteLine($"     - {evt.Name}[{evt.Offset}]: {evt.OldValue} -> {evt.NewValue}");
            }
            Environment.Exit(0);
        } else {
            Console.WriteLine($"❌ FAIL - C#: Expected 3 changes, got {tracker.EventCount}");
            Environment.Exit(1);
        }
    }
}
