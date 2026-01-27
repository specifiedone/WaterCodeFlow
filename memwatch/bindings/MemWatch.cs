// C# / .NET binding for memwatch
// Place in: csharp/MemWatch.cs

using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace MemWatch {
    
    /// <summary>
    /// Change event - unified across all language bindings
    /// </summary>
    public class ChangeEvent {
        public uint Seq { get; set; }
        public ulong TimestampNs { get; set; }
        public uint AdapterId { get; set; }
        public uint RegionId { get; set; }
        public string VariableName { get; set; }
        
        public class Location {
            public string File { get; set; }
            public string Function { get; set; }
            public uint Line { get; set; }
            public ulong FaultIp { get; set; }
        }
        
        public Location Where { get; set; }
        public byte[] OldPreview { get; set; }
        public byte[] NewPreview { get; set; }
        public byte[] OldValue { get; set; }
        public byte[] NewValue { get; set; }
        public string StorageKeyOld { get; set; }
        public string StorageKeyNew { get; set; }
        public Dictionary<string, object> Metadata { get; set; }
        
        public ChangeEvent() {
            Where = new Location();
            Metadata = new Dictionary<string, object>();
        }
    }
    
    /// <summary>
    /// MemWatch - Unified memory watcher for C#/.NET
    /// 
    /// Example:
    ///   var watcher = new MemoryWatcher();
    ///   var data = System.Text.Encoding.UTF8.GetBytes("hello");
    ///   watcher.Watch(data, "my_data");
    ///   watcher.OnChange += (event) => Console.WriteLine(event.VariableName + " changed");
    ///   data[0] = 72; // Triggers event
    /// </summary>
    public class MemoryWatcher : IDisposable {
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        private static extern int memwatch_init();
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        private static extern void memwatch_shutdown();
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        private static extern uint memwatch_watch(ulong addr, int size, string name, IntPtr user_data);
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool memwatch_unwatch(uint region_id);
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        private static extern int memwatch_set_callback(IntPtr callback, IntPtr user_ctx);
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        private static extern int memwatch_check_changes(
            [Out] ChangeEventNative[] out_events, int max_events);
        
        [DllImport("memwatch_native", CallingConvention = CallingConvention.Cdecl)]
        private static extern int memwatch_get_stats(out MemWatchStatsNative stats);
        
        // Internal native structures
        [StructLayout(LayoutKind.Sequential)]
        private struct ChangeEventNative {
            public uint seq;
            public ulong timestamp_ns;
            public uint adapter_id;
            public uint region_id;
            [MarshalAs(UnmanagedType.LPStr)]
            public string variable_name;
            [MarshalAs(UnmanagedType.LPStr)]
            public string file;
            [MarshalAs(UnmanagedType.LPStr)]
            public string function;
            public uint line;
            public ulong fault_ip;
            public IntPtr old_preview;
            public int old_preview_size;
            public IntPtr new_preview;
            public int new_preview_size;
        }
        
        [StructLayout(LayoutKind.Sequential)]
        private struct MemWatchStatsNative {
            public uint num_tracked_regions;
            public uint num_active_watchpoints;
            public ulong total_events;
            public ulong ring_write_count;
            public ulong ring_drop_count;
            public ulong storage_bytes_used;
            public uint mprotect_page_count;
            public uint worker_thread_id;
            public ulong worker_cycles;
        }
        
        public class MemWatchStats {
            public uint NumTrackedRegions { get; set; }
            public uint NumActiveWatchpoints { get; set; }
            public ulong TotalEvents { get; set; }
            public ulong RingWriteCount { get; set; }
            public ulong RingDropCount { get; set; }
            public ulong StorageBytesUsed { get; set; }
        }
        
        /// <summary>Delegate for change events</summary>
        public delegate void ChangeEventHandler(ChangeEvent @event);
        
        /// <summary>Event fired when tracked memory changes</summary>
        public event ChangeEventHandler OnChange;
        
        private Dictionary<uint, object> trackedObjects;
        private GCHandle callbackHandle;
        
        /// <summary>Create a new MemoryWatcher</summary>
        public MemoryWatcher() {
            trackedObjects = new Dictionary<uint, object>();
            
            int result = memwatch_init();
            if (result != 0) {
                throw new InvalidOperationException($"Failed to initialize memwatch: {result}");
            }
        }
        
        /// <summary>Watch a byte array for changes</summary>
        public uint Watch(byte[] buffer, string name = null) {
            if (buffer == null) {
                throw new ArgumentNullException(nameof(buffer));
            }
            
            // Pin buffer and get address
            GCHandle handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try {
                IntPtr addr = Marshal.UnsafeAddrOfPinnedArrayElement(buffer, 0);
                uint region_id = memwatch_watch((ulong)addr, buffer.Length, name, IntPtr.Zero);
                
                if (region_id > 0) {
                    trackedObjects[region_id] = buffer; // Keep reference alive
                }
                
                return region_id;
            } finally {
                handle.Free();
            }
        }
        
        /// <summary>Stop watching a region</summary>
        public bool Unwatch(uint region_id) {
            bool result = memwatch_unwatch(region_id);
            if (result) {
                trackedObjects.Remove(region_id);
            }
            return result;
        }
        
        /// <summary>Set callback for change events</summary>
        public void SetCallback(ChangeEventHandler handler) {
            if (handler != null) {
                // Create callback delegate
                MemwatchCallbackDelegate callback = (IntPtr evt_ptr, IntPtr ctx) => {
                    // Marshal event from native
                    var evt = Marshal.PtrToStructure<ChangeEventNative>(evt_ptr);
                    var managed_evt = new ChangeEvent {
                        Seq = evt.seq,
                        TimestampNs = evt.timestamp_ns,
                        AdapterId = evt.adapter_id,
                        RegionId = evt.region_id,
                        VariableName = evt.variable_name,
                        Where = new ChangeEvent.Location {
                            File = evt.file,
                            Function = evt.function,
                            Line = evt.line,
                            FaultIp = evt.fault_ip
                        }
                    };
                    
                    OnChange?.Invoke(managed_evt);
                };
                
                // Keep delegate alive
                callbackHandle = GCHandle.Alloc(callback);
                IntPtr cb_ptr = Marshal.GetFunctionPointerForDelegate(callback);
                memwatch_set_callback(cb_ptr, IntPtr.Zero);
            } else {
                memwatch_set_callback(IntPtr.Zero, IntPtr.Zero);
                if (callbackHandle.IsAllocated) {
                    callbackHandle.Free();
                }
            }
        }
        
        /// <summary>Synchronously check for changes (polling mode)</summary>
        public List<ChangeEvent> CheckChanges() {
            ChangeEventNative[] native_events = new ChangeEventNative[16];
            int count = memwatch_check_changes(native_events, 16);
            
            var result = new List<ChangeEvent>();
            for (int i = 0; i < count; i++) {
                result.Add(new ChangeEvent {
                    Seq = native_events[i].seq,
                    TimestampNs = native_events[i].timestamp_ns,
                    AdapterId = native_events[i].adapter_id,
                    RegionId = native_events[i].region_id,
                    VariableName = native_events[i].variable_name,
                    Where = new ChangeEvent.Location {
                        File = native_events[i].file,
                        Function = native_events[i].function,
                        Line = native_events[i].line,
                        FaultIp = native_events[i].fault_ip
                    }
                });
            }
            
            return result;
        }
        
        /// <summary>Get statistics</summary>
        public MemWatchStats GetStats() {
            memwatch_get_stats(out var native_stats);
            return new MemWatchStats {
                NumTrackedRegions = native_stats.num_tracked_regions,
                NumActiveWatchpoints = native_stats.num_active_watchpoints,
                TotalEvents = native_stats.total_events,
                RingWriteCount = native_stats.ring_write_count,
                RingDropCount = native_stats.ring_drop_count,
                StorageBytesUsed = native_stats.storage_bytes_used,
            };
        }
        
        /// <summary>Clean shutdown</summary>
        public void Shutdown() {
            memwatch_shutdown();
        }
        
        public void Dispose() {
            Shutdown();
            if (callbackHandle.IsAllocated) {
                callbackHandle.Free();
            }
        }
        
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void MemwatchCallbackDelegate(IntPtr @event, IntPtr user_ctx);
    }
}
