package main

import "testing"

func TestVersionCode(t *testing.T) {
	for v, want := range map[string]int{
		"espos-x4-0.4.0": 400, "espos-s3-0.5.0": 500, "espos-x4-1.0.0": 10000, "0.2.10": 210, "bad": 0, "espos-x4-0.3": 0,
		"espos-x4-0.3.2": 302, "espos-s3-0.4.5": 405,
	} {
		if got := versionCode(v); got != want {
			t.Errorf("versionCode(%q) = %d, want %d", v, got, want)
		}
	}
}
