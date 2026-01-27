from memwatch import MemoryWatcher

watcher = MemoryWatcher()

# Track a buffer
data = bytearray(b"Hello")
watcher.watch(data, name="data")

# Set callback
def on_change(event):
    print(f"{event.variable_name} changed!")
    print(f"Old: {event.old_value}")
    print(f"New: {event.new_value}")

watcher.set_callback(on_change)

# Modify - callback fires!
data[0] = ord('J')