package news

import (
	"archive/zip"
	"bytes"
	"fmt"
	"html"
	"strings"
	"time"
)

const css = `body { margin: 0 4%; font-family: serif; line-height: 1.45; }
p { margin: 0 0 0.8em 0; text-indent: 0; text-align: justify; }
p.kick { font-family: sans-serif; font-weight: bold; font-size: 0.75em; letter-spacing: 0.05em; margin: 0.6em 0 0.3em 0; }
h1 { font-size: 1.45em; line-height: 1.25; margin: 0 0 0.5em 0; text-align: left; }
p.lead { font-weight: bold; text-align: left; }
div.fig { margin: 0.8em 0; text-align: center; }
div.fig img { max-width: 100%; }
p.cap { font-family: sans-serif; font-style: italic; font-size: 0.8em; text-align: left; margin-top: 0.3em; }
p.src { font-family: sans-serif; font-size: 0.75em; text-align: left; margin-top: 1.2em; }
`

func xhtml(title, body string) string {
	return `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="vi" lang="vi">
<head><meta charset="utf-8"/><title>` + html.EscapeString(title) + `</title><link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
` + body + `
</body>
</html>
`
}

// Image is one picture stored in the EPUB.
type Image struct {
	File string // name inside OEBPS/img
	Data []byte
}

// EPUB packs the issue: cover, one chapter per article, nested table of contents
// (section → article), EPUB 3 nav plus an EPUB 2 NCX for older readers.
func EPUB(title string, date time.Time, articles []Article, cover []byte, images []Image) ([]byte, error) {
	var buf bytes.Buffer
	z := zip.NewWriter(&buf)
	// mimetype first and stored, as the spec requires.
	w, err := z.CreateHeader(&zip.FileHeader{Name: "mimetype", Method: zip.Store})
	if err != nil {
		return nil, err
	}
	w.Write([]byte("application/epub+zip"))
	add := func(name, content string) error {
		f, err := z.Create(name)
		if err != nil {
			return err
		}
		_, err = f.Write([]byte(content))
		return err
	}
	addBin := func(name string, data []byte) error {
		f, err := z.CreateHeader(&zip.FileHeader{Name: name, Method: zip.Store}) // JPEGs do not compress
		if err != nil {
			return err
		}
		_, err = f.Write(data)
		return err
	}
	if err := add("META-INF/container.xml", `<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
`); err != nil {
		return nil, err
	}
	if err := add("OEBPS/style.css", css); err != nil {
		return nil, err
	}
	if err := addBin("OEBPS/img/cover.jpg", cover); err != nil {
		return nil, err
	}
	if err := add("OEBPS/cover.xhtml", xhtml(title, `<div style="text-align:center"><img src="img/cover.jpg" alt="`+html.EscapeString(title)+`" style="max-width:100%"/></div>`)); err != nil {
		return nil, err
	}
	for _, im := range images {
		if err := addBin("OEBPS/img/"+im.File, im.Data); err != nil {
			return nil, err
		}
	}

	var manifest, spine, nav, ncx strings.Builder
	manifest.WriteString(`<item id="css" href="style.css" media-type="text/css"/>
<item id="cover-image" href="img/cover.jpg" media-type="image/jpeg" properties="cover-image"/>
<item id="cover" href="cover.xhtml" media-type="application/xhtml+xml"/>
<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
`)
	for _, im := range images {
		fmt.Fprintf(&manifest, `<item id="i-%s" href="img/%s" media-type="image/jpeg"/>`+"\n", strings.TrimSuffix(im.File, ".jpg"), im.File)
	}
	spine.WriteString(`<itemref idref="cover"/>` + "\n")

	// The cover first, so the reader has a title for it.
	nav.WriteString(`<li><a href="cover.xhtml">Bìa</a></li>` + "\n")
	ncx.WriteString(`<navPoint id="cover" playOrder="1"><navLabel><text>Bìa</text></navLabel><content src="cover.xhtml"/></navPoint>` + "\n")
	play := 2
	curSec := ""
	navOpen := false
	ncxOpen := false
	for i, a := range articles {
		id := fmt.Sprintf("a%03d", i+1)
		file := id + ".xhtml"
		var body strings.Builder
		pub := ""
		if !a.Pub.IsZero() {
			pub = " · " + a.Pub.In(date.Location()).Format("02/01 15:04")
		}
		fmt.Fprintf(&body, "<p class=\"kick\">%s%s</p>\n<h1>%s</h1>\n", html.EscapeString(strings.ToUpper(a.Section.Name)), pub, html.EscapeString(a.Title))
		if a.Lead != "" {
			fmt.Fprintf(&body, "<p class=\"lead\">%s</p>\n", html.EscapeString(a.Lead))
		}
		for _, b := range a.Blocks {
			switch {
			case b.Para != "":
				fmt.Fprintf(&body, "<p>%s</p>\n", b.Para)
			case b.ImgFile != "":
				fmt.Fprintf(&body, "<div class=\"fig\"><img src=\"img/%s\" alt=\"\"/>", b.ImgFile)
				if b.Caption != "" {
					fmt.Fprintf(&body, "<p class=\"cap\">%s</p>", html.EscapeString(b.Caption))
				}
				body.WriteString("</div>\n")
			}
		}
		fmt.Fprintf(&body, "<p class=\"src\">Nguồn: VnExpress · %s</p>", html.EscapeString(a.URL))
		if err := add("OEBPS/"+file, xhtml(a.Title, body.String())); err != nil {
			return nil, err
		}
		fmt.Fprintf(&manifest, `<item id="%s" href="%s" media-type="application/xhtml+xml"/>`+"\n", id, file)
		fmt.Fprintf(&spine, `<itemref idref="%s"/>`+"\n", id)

		if a.Section.Slug != curSec {
			if navOpen {
				nav.WriteString("</ol></li>\n")
				ncx.WriteString("</navPoint>\n")
			}
			curSec = a.Section.Slug
			fmt.Fprintf(&nav, `<li><a href="%s">%s</a><ol>`+"\n", file, html.EscapeString(a.Section.Name))
			fmt.Fprintf(&ncx, `<navPoint id="s%d" playOrder="%d"><navLabel><text>%s</text></navLabel><content src="%s"/>`+"\n", play, play, html.EscapeString(a.Section.Name), file)
			play++
			navOpen, ncxOpen = true, true
		}
		fmt.Fprintf(&nav, `<li><a href="%s">%s</a></li>`+"\n", file, html.EscapeString(a.Title))
		fmt.Fprintf(&ncx, `<navPoint id="n%d" playOrder="%d"><navLabel><text>%s</text></navLabel><content src="%s"/></navPoint>`+"\n", play, play, html.EscapeString(a.Title), file)
		play++
	}
	if navOpen {
		nav.WriteString("</ol></li>\n")
	}
	if ncxOpen {
		ncx.WriteString("</navPoint>\n")
	}

	uid := "urn:espos:bao-sang:" + date.Format("2006-01-02")
	if err := add("OEBPS/nav.xhtml", xhtml("Mục lục", `<nav epub:type="toc" id="toc"><h1>Mục lục</h1><ol>
`+nav.String()+`</ol></nav>`)); err != nil {
		return nil, err
	}
	if err := add("OEBPS/toc.ncx", `<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
<head><meta name="dtb:uid" content="`+uid+`"/><meta name="dtb:depth" content="2"/></head>
<docTitle><text>`+html.EscapeString(title)+`</text></docTitle>
<navMap>
`+ncx.String()+`</navMap>
</ncx>
`); err != nil {
		return nil, err
	}
	opf := `<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid" xml:lang="vi">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="uid">` + uid + `</dc:identifier>
<dc:title>` + html.EscapeString(title) + `</dc:title>
<dc:creator>VnExpress</dc:creator>
<dc:publisher>EspOS</dc:publisher>
<dc:language>vi</dc:language>
<dc:date>` + date.Format("2006-01-02") + `</dc:date>
<meta property="dcterms:modified">` + time.Now().UTC().Format("2006-01-02T15:04:05Z") + `</meta>
<meta name="cover" content="cover-image"/>
</metadata>
<manifest>
` + manifest.String() + `</manifest>
<spine toc="ncx">
` + spine.String() + `</spine>
</package>
`
	if err := add("OEBPS/content.opf", opf); err != nil {
		return nil, err
	}
	if err := z.Close(); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
