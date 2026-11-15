// Package backend holds the workflow declarations: routes, tables and workflow graphs
// (imported from the hosted site by tools/importer).
package backend

import "embed"

// FS has routes.json, tables/*.json and workflows/<code>/workflow.json.
//
//go:embed routes.json tables workflows
var FS embed.FS
