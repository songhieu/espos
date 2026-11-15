# make            build the web app, then the server binary (./inkdesk)
# make run        build and run with ./data (set INKDESK_PASSWORD first)
# make test       Go tests
# make import FROM=../path/to/hosted/backend    re-import the workflows

.PHONY: all web server run test import clean

all: web server

web:
	cd web && npm ci --no-audit --no-fund && npm run build
	touch internal/server/dist/.keep

server:
	CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o inkdesk ./cmd/inkdesk

run: all
	./inkdesk

test:
	go test ./...

import:
	@test -n "$(FROM)" || (echo "usage: make import FROM=../web/backend" && exit 2)
	go run ./tools/importer -from $(FROM)
	gofmt -l internal || true

clean:
	rm -f inkdesk
	find internal/server/dist -mindepth 1 ! -name .keep -delete
