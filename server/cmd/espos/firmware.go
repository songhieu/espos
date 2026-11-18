package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"espos/internal/wm"
)

// A firmware bundle is a folder with manifest.json and the .bin parts it names:
//
//	{"version": "espos-x4-0.4.0", "chip": "esp32c3", "flash_size": "16MB",
//	 "parts": [{"file": "bootloader.bin", "offset": 0}, …, {"file": "firmware.bin", "offset": 65536}]}
//
// `firmware add` copies the parts into <data>/firmware (the web installer flashes them over
// USB from /firmware/latest.json) and offers firmware.bin to linked readers as an update.

type part struct {
	File   string `json:"file"`
	Offset int    `json:"offset"`
	Size   int    `json:"size,omitempty"`
	SHA256 string `json:"sha256,omitempty"`
	URL    string `json:"url,omitempty"`
}

type manifest struct {
	Version   string `json:"version"`
	Chip      string `json:"chip,omitempty"`
	FlashSize string `json:"flash_size,omitempty"`
	Parts     []part `json:"parts"`
}

func firmwareCmd(args []string) {
	if len(args) < 2 || args[0] != "add" {
		fmt.Fprintln(os.Stderr, "usage: espos firmware add <bundle-dir> [-notes TEXT]")
		os.Exit(2)
	}
	fl := flag.NewFlagSet("firmware add", flag.ExitOnError)
	notes := fl.String("notes", "", "what changed (shown on the Devices page)")
	dir := args[1]
	_ = fl.Parse(args[2:])

	raw, err := os.ReadFile(filepath.Join(dir, "manifest.json"))
	if err != nil {
		log.Fatal(err)
	}
	var m manifest
	if err := json.Unmarshal(raw, &m); err != nil {
		log.Fatal(err)
	}
	code := versionCode(m.Version)
	if code <= 0 {
		log.Fatalf("version %q is not like name-MAJOR.MINOR.PATCH", m.Version)
	}

	dataDir := env("ESPOS_DATA", "./data")
	open(dataDir)
	defer wm.Close()
	out := filepath.Join(dataDir, "firmware")
	if err := os.MkdirAll(out, 0o700); err != nil {
		log.Fatal(err)
	}
	var app part
	for i, p := range m.Parts {
		if p.File != filepath.Base(p.File) || strings.HasPrefix(p.File, ".") || !strings.HasSuffix(p.File, ".bin") {
			log.Fatalf("manifest part %q: expected a plain .bin file name", p.File)
		}
		b, err := os.ReadFile(filepath.Join(dir, p.File))
		if err != nil {
			log.Fatal(err)
		}
		sum := sha256.Sum256(b)
		hexSum := hex.EncodeToString(sum[:])
		name := strings.TrimSuffix(p.File, ".bin") + "-" + hexSum[:12] + ".bin"
		if err := os.WriteFile(filepath.Join(out, name), b, 0o644); err != nil {
			log.Fatal(err)
		}
		m.Parts[i].Size, m.Parts[i].SHA256, m.Parts[i].URL = len(b), hexSum, "/firmware/"+name
		if p.File == "firmware.bin" {
			app = m.Parts[i]
		}
		fmt.Printf("%-16s %9d B @ 0x%x\n", p.File, len(b), p.Offset)
	}
	if app.File == "" {
		log.Fatal("the bundle has no firmware.bin")
	}
	if m.Chip == "" {
		m.Chip = "esp32c3"
	}
	latest, _ := json.MarshalIndent(m, "", " ")
	if err := os.WriteFile(filepath.Join(out, "latest.json"), latest, 0o644); err != nil {
		log.Fatal(err)
	}
	if _, err := wm.Upsert("firmware", []string{"code"}, map[string]any{
		"version": m.Version, "code": code, "url": app.URL, "sha256": app.SHA256, "size": app.Size,
		"notes": *notes, "released": time.Now().Format("2006-01-02"),
	}, nil); err != nil {
		log.Fatal(err)
	}
	fmt.Printf("%s is now offered to readers (code %d)\n", m.Version, code)
}

// versionCode: "espos-x4-0.4.0" → 400 (the firmware compares versions the same way; older
// builds are named like "<name>-0.3.2").
func versionCode(v string) int {
	if i := strings.LastIndex(v, "-"); i >= 0 {
		v = v[i+1:]
	}
	parts := strings.Split(v, ".")
	if len(parts) != 3 {
		return 0
	}
	n := 0
	for _, p := range parts {
		x, err := strconv.Atoi(p)
		if err != nil || x < 0 || x > 99 {
			return 0
		}
		n = n*100 + x
	}
	return n
}
