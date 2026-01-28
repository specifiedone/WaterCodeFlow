#!/usr/bin/env python3
"""
Callback function for memwatch CLI

This function is called whenever a memory change is detected.
The function MUST be named 'main' for memwatch to invoke it.

It receives event data in JSON format at /tmp/memwatch_event_*.json
"""

import json
import os
import sys
from pathlib import Path

def main():
    """
    Main callback function - called on each memory change event
    
    Usage:
        memwatch run ./program --user-func my_callback.py --user-func-lang python
    """
    
    # Find the latest event file
    tmp_dir = Path("/tmp")
    event_files = list(tmp_dir.glob("memwatch_event_*.json"))
    
    if not event_files:
        return
    
    # Get the most recent event
    event_file = max(event_files, key=lambda p: p.stat().st_mtime)
    
    try:
        with open(event_file, 'r') as f:
            event = json.load(f)
        
        # Process the event
        print(f"🔔 [Callback] Memory change detected!")
        print(f"   Variable: {event.get('variable', 'unknown')}")
        print(f"   Old value: {event.get('old_value', 'unknown')}")
        print(f"   New value: {event.get('new_value', 'unknown')}")
        print(f"   Region ID: {event.get('region_id', 0)}")
        
        # You can add custom logic here:
        # - Alert on specific changes
        # - Log to file
        # - Send to remote server
        # - Trigger other actions
        
    except Exception as e:
        print(f"❌ Callback error: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
