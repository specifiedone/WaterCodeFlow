package main

import (
	"fmt"
)

type MemoryEvent struct {
	Name     string
	Offset   int
	OldValue int
	NewValue int
}

type MemoryTracker struct {
	regions      map[int][]byte
	initial      map[int][]byte
	events       []MemoryEvent
	regionCount  int
}

func NewMemoryTracker() *MemoryTracker {
	return &MemoryTracker{
		regions:      make(map[int][]byte),
		initial:      make(map[int][]byte),
		events:       make([]MemoryEvent, 0),
		regionCount:  0,
	}
}

func (mt *MemoryTracker) Watch(data []byte, name string) int {
	id := mt.regionCount
	mt.regionCount++
	
	// Clone the data
	dataCopy := make([]byte, len(data))
	copy(dataCopy, data)
	
	initialCopy := make([]byte, len(data))
	copy(initialCopy, data)
	
	mt.regions[id] = dataCopy
	mt.initial[id] = initialCopy
	
	fmt.Printf("  ✓ Watching region %d: %s\n", id, name)
	return id
}

func (mt *MemoryTracker) DetectChanges() {
	for id, region := range mt.regions {
		init := mt.initial[id]
		
		for i := 0; i < len(region); i++ {
			if init[i] != region[i] {
				mt.events = append(mt.events, MemoryEvent{
					Name:     fmt.Sprintf("region_%d", id),
					Offset:   i,
					OldValue: int(init[i]),
					NewValue: int(region[i]),
				})
				init[i] = region[i]
			}
		}
	}
}

func main() {
	fmt.Println("🧪 Go Memory Tracking Test")
	fmt.Println("==========================")
	
	tracker := NewMemoryTracker()
	data := make([]byte, 20)
	
	regionId := tracker.Watch(data, "test_buffer")
	_ = regionId
	
	data[0] = 42
	data[5] = 99
	data[10] = 255
	
	// Update the tracker with modified data
	tracker.regions[regionId] = data
	
	tracker.DetectChanges()
	
	if len(tracker.events) == 3 {
		fmt.Printf("✅ PASS - Go: Detected %d changes\n", len(tracker.events))
		fmt.Println("   Events:")
		for _, evt := range tracker.events {
			fmt.Printf("     - %s[%d]: %d -> %d\n", evt.Name, evt.Offset, evt.OldValue, evt.NewValue)
		}
	} else {
		fmt.Printf("❌ FAIL - Go: Expected 3 changes, got %d\n", len(tracker.events))
	}
}
