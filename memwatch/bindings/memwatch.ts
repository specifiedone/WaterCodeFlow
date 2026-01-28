/**
 * memwatch - TypeScript binding
 * 
 * Unified API: same as all other language bindings
 * Provides full TypeScript type definitions for memory watching
 */

const native = require('./build/Release/memwatch_native');

interface ChangeEventData {
  seq: number;
  timestamp_ns: bigint;
  variable_name?: string;
  where?: {
    file?: string;
    function?: string;
    line: number;
    fault_ip: bigint;
  };
  old_preview?: Uint8Array;
  new_preview?: Uint8Array;
  old_value?: Uint8Array;
  new_value?: Uint8Array;
  storage_key_old?: string;
  storage_key_new?: string;
}

class ChangeEvent {
  seq: number;
  timestamp_ns: bigint;
  variable_name?: string;
  where?: {
    file?: string;
    function?: string;
    line: number;
    fault_ip: bigint;
  };
  old_preview?: Uint8Array;
  new_preview?: Uint8Array;
  old_value?: Uint8Array;
  new_value?: Uint8Array;
  storage_key_old?: string;
  storage_key_new?: string;

  constructor(data: ChangeEventData) {
    this.seq = data.seq;
    this.timestamp_ns = data.timestamp_ns;
    this.variable_name = data.variable_name;
    this.where = data.where;
    this.old_preview = data.old_preview;
    this.new_preview = data.new_preview;
    this.old_value = data.old_value;
    this.new_value = data.new_value;
    this.storage_key_old = data.storage_key_old;
    this.storage_key_new = data.storage_key_new;
  }
}

interface StatsData {
  num_tracked_regions: number;
  num_active_watchpoints: number;
  total_events: bigint;
  ring_write_count: bigint;
  ring_drop_count: bigint;
  storage_bytes_used: bigint;
  mprotect_page_count: number;
  worker_thread_id: number;
  worker_cycles: bigint;
}

class Stats {
  num_tracked_regions: number;
  num_active_watchpoints: number;
  total_events: bigint;
  ring_write_count: bigint;
  ring_drop_count: bigint;
  storage_bytes_used: bigint;
  mprotect_page_count: number;
  worker_thread_id: number;
  worker_cycles: bigint;

  constructor(data: StatsData) {
    this.num_tracked_regions = data.num_tracked_regions;
    this.num_active_watchpoints = data.num_active_watchpoints;
    this.total_events = data.total_events;
    this.ring_write_count = data.ring_write_count;
    this.ring_drop_count = data.ring_drop_count;
    this.storage_bytes_used = data.storage_bytes_used;
    this.mprotect_page_count = data.mprotect_page_count;
    this.worker_thread_id = data.worker_thread_id;
    this.worker_cycles = data.worker_cycles;
  }
}

type ChangeEventCallback = (event: ChangeEvent) => void;

interface WatchedRegion {
  buffer: Buffer | ArrayBufferView;
  name?: string;
  max_value_bytes: number;
}

class MemWatch {
  private _callback: ChangeEventCallback | null = null;
  private _regions: Map<number, WatchedRegion> = new Map();

  constructor() {
    native.init();
  }

  /**
   * Watch a buffer or TypedArray for changes
   * @param buffer - Buffer or TypedArray to watch
   * @param name - Variable name (optional)
   * @param max_value_bytes - Max value storage: 0=none, >0=limit to N bytes, -1=full (default 256)
   * @returns region_id
   */
  watch(buffer: Buffer | ArrayBufferView, name?: string, max_value_bytes: number = 256): number {
    if (!Buffer.isBuffer(buffer) && !ArrayBuffer.isView(buffer)) {
      throw new TypeError('Expected Buffer or TypedArray');
    }

    let addr: number;
    let size: number;
    
    if (Buffer.isBuffer(buffer)) {
      addr = (buffer as any).buffer.byteOffset + buffer.byteOffset;
      size = buffer.byteLength;
    } else {
      const view = buffer as ArrayBufferView;
      addr = (view as any).buffer.byteOffset + (view.byteOffset || 0);
      size = view.byteLength;
    }

    const region_id = native.watch_with_max_value_bytes(BigInt(addr), size, name || null, max_value_bytes) as number;
    this._regions.set(region_id, { buffer, name, max_value_bytes });
    return region_id;
  }

  /**
   * Stop watching a region
   * @param region_id - ID from watch()
   * @returns true if successful
   */
  unwatch(region_id: number): boolean {
    if (native.unwatch(region_id)) {
      this._regions.delete(region_id);
      return true;
    }
    return false;
  }

  /**
   * Set callback for change events
   * @param fn - Callback function or null
   */
  set_callback(fn: ChangeEventCallback | null): void {
    this._callback = fn;
    if (fn) {
      native.set_callback((event: ChangeEventData) => {
        fn(new ChangeEvent(event));
      });
    } else {
      native.set_callback(null);
    }
  }

  /**
   * Synchronously check for changes (polling mode)
   * @returns Array of ChangeEvent objects
   */
  check_changes(): ChangeEvent[] {
    const events = native.check_changes() as ChangeEventData[];
    return events.map(event => new ChangeEvent(event));
  }

  /**
   * Get current statistics
   * @returns Stats object
   */
  get_stats(): Stats {
    const data = native.get_stats() as StatsData;
    return new Stats(data);
  }

  /**
   * Shutdown memwatch
   */
  shutdown(): void {
    native.shutdown();
  }

  /**
   * Get list of watched regions
   * @returns Array of region IDs
   */
  get watched_regions(): number[] {
    return Array.from(this._regions.keys());
  }

  /**
   * Get information about a watched region
   * @param region_id - ID from watch()
   * @returns Region info or undefined
   */
  get_region_info(region_id: number): WatchedRegion | undefined {
    return this._regions.get(region_id);
  }
}

export { MemWatch, ChangeEvent, Stats, ChangeEventCallback, ChangeEventData, StatsData };
export default MemWatch;
