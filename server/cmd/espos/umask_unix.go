//go:build !windows

package main

import "syscall"

// restrictUmask keeps every file the server creates private to its own user.
func restrictUmask() { syscall.Umask(0o077) }
