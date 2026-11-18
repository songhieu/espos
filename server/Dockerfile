# Build the web app, then a static Go binary, then a small image with only the binary.
FROM node:22-alpine AS web
WORKDIR /src/web
COPY web/package.json web/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY web/ ./
RUN mkdir -p ../internal/server/dist && npm run build

FROM golang:1.26-alpine AS server
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
COPY --from=web /src/internal/server/dist ./internal/server/dist
RUN CGO_ENABLED=0 go build -trimpath -ldflags "-s -w" -o /espos ./cmd/espos

FROM alpine:3.20
RUN apk add --no-cache ca-certificates tzdata && adduser -D -H -u 10001 espos && mkdir /data && chown espos /data
COPY --from=server /espos /usr/local/bin/espos
USER espos
ENV ESPOS_DATA=/data ESPOS_ADDR=:8080
VOLUME /data
EXPOSE 8080
ENTRYPOINT ["espos"]
