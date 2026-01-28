/**
 * memwatch - Node.js/JavaScript binding
 * 
 * Unified API: same as all other language bindings
 */

const native = require('./build/Release/memwatch_native');

class ChangeEvent {
  constructor(data) {
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

class MemWatch {
  constructor() {
    this._callback = null;
    this._regions = new Map();
    native.init();
  }

  /**
   * Watch a buffer or TypedArray for changes
   * @param {Buffer|TypedArray} buffer - Buffer to watch
   * @param {string} name - Variable name (optional)
   * @param {number} max_value_bytes - Max value storage: 0=none, >0=limit to N bytes, -1=full (default 256)
   * @returns {number} region_id
   */
  watch(buffer, name = null, max_value_bytes = 256) {
    if (!Buffer.isBuffer(buffer) && !ArrayBuffer.isView(buffer)) {
      throw new TypeError('Expected Buffer or TypedArray');
    }

    const addr = buffer.buffer.byteOffset + (buffer.byteOffset || 0);
    const size = buffer.byteLength;

    const region_id = native.watch_with_max_value_bytes(BigInt(addr), size, name || null, max_value_bytes);
    this._regions.set(region_id, { buffer, name, max_value_bytes });
    return region_id;
  }

  /**
   * Stop watching a region
   * @param {number} region_id - ID from watch()
   * @returns {boolean}
   */
  unwatch(region_id) {
    if (native.unwatch(region_id)) {
      this._regions.delete(region_id);
      return true;
    }
    return false;
  }

  /**
   * Set callback for change events
   * @param {Function|null} fn - Callback function or null
   */
  set_callback(fn) {
    this._callback = fn;
    if (fn) {
      native.set_callback((event) => {
        fn(new ChangeEvent(event));
      });
    } else {
      native.set_callback(null);
    }
  }

  /**
   * Synchronously check for changes (polling mode)
   * @returns {ChangeEvent[]}
   */
  check_changes() {
    return native.check_changes().map(e => new ChangeEvent(e));
  }

  /**
   * Get statistics
   * @returns {object}
   */
  get_stats() {
    return native.get_stats();
  }

  /**
   * Clean shutdown
   */
  shutdown() {
    native.shutdown();
  }

  /**
   * Event emitter pattern for compatibility
   */
  on(event, handler) {
    if (event === 'change') {
      this.set_callback(handler);
    }
  }

  off(event, handler) {
    if (event === 'change') {
      this.set_callback(null);
    }
  }
}

/**
 * Factory function (matches all language bindings)
 */
function create() {
  return new MemWatch();
}

module.exports = {
  MemWatch,
  ChangeEvent,
  create,
};
