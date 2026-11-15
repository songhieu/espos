package main

import "testing"

func TestVersionCode(t *testing.T) {
	for v, want := range map[string]int{"inkdesk-0.3.2": 302, "inkdesk-1.0.0": 10000, "0.2.10": 210, "bad": 0, "inkdesk-0.3": 0} {
		if got := versionCode(v); got != want {
			t.Errorf("versionCode(%q) = %d, want %d", v, got, want)
		}
	}
}
