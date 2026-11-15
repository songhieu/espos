package wm

import "strings"

// The address the device and the browser reach this server at, for the links the nodes
// write into answers (a book's download URL, …).

var publicURL string // INKDESK_PUBLIC_URL, when set

// SetPublicURL fixes the server's public address ("https://inkdesk.example.com").
func SetPublicURL(u string) { publicURL = strings.TrimRight(u, "/") }

// PublicBase is "scheme://host" for links to this server: the configured public URL, else
// what the request came to. The server hands nodes "scheme://host" as the request's host;
// a bare host name is taken as plain http.
func PublicBase(host string) string {
	if publicURL != "" {
		return publicURL
	}
	if strings.Contains(host, "://") {
		return strings.TrimRight(host, "/")
	}
	if host == "" {
		host = "localhost:8080"
	}
	return "http://" + host
}

// AbsURL makes a link stored as a path ("/firmware/x.bin") absolute for the device.
func AbsURL(host, u string) string {
	if u == "" || strings.HasPrefix(u, "http://") || strings.HasPrefix(u, "https://") {
		return u
	}
	return PublicBase(host) + "/" + strings.TrimLeft(u, "/")
}
