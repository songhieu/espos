package server

import (
	"embed"
	"io/fs"
)

// The built web app (`make web` writes it here; see README).
//
//go:embed all:dist
var dist embed.FS

// App is the web app's files.
func App() fs.FS {
	sub, err := fs.Sub(dist, "dist")
	if err != nil {
		panic(err)
	}
	return sub
}
