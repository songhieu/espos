package main

import (
	"bufio"
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

var quickURL = regexp.MustCompile(`https://[a-z0-9-]+\.trycloudflare\.com`)

// runTunnel keeps a cloudflared process connected to Cloudflare until ctx ends.
//
//	mode token: named tunnel created in the Cloudflare dashboard (stable hostname
//	            on your own domain; set its Public Hostname service to
//	            http://localhost:<port>).
//	mode quick: no account/domain needed; a random https://*.trycloudflare.com
//	            URL that changes on every restart (for testing).
func (g *Gateway) runTunnel(ctx context.Context, wg *sync.WaitGroup) {
	defer wg.Done()
	t := g.cfg.Tunnel
	bin, err := exec.LookPath(t.Cloudflared)
	if err != nil {
		log.Printf("[tunnel] cloudflared not found (%v) — install with: brew install cloudflared. "+
			"The API still runs on %s", err, g.cfg.Listen)
		return
	}
	local := "http://" + localAddr(g.cfg.Listen)
	pidFile := filepath.Join(g.cfg.BaseDir, ".cloudflared.pid")
	killStale(pidFile)
	defer os.Remove(pidFile)
	backoff := 2 * time.Second
	for ctx.Err() == nil {
		var args []string
		env := os.Environ()
		if t.Mode == "quick" {
			args = []string{"tunnel", "--no-autoupdate", "--url", local}
		} else {
			// --url is the origin for tunnels created with `cloudflared tunnel create`
			// (no ingress stored at Cloudflare); dashboard tunnels point at the same place.
			args = []string{"tunnel", "--no-autoupdate", "--url", local, "run"}
			env = append(env, "TUNNEL_TOKEN="+t.Token) // env, not argv: keeps the token out of `ps`
		}
		cmd := exec.Command(bin, args...)
		cmd.Env = env
		cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
		out, _ := cmd.StderrPipe()
		cmd.Stdout = cmd.Stderr
		started := time.Now()
		if err := cmd.Start(); err != nil {
			log.Printf("[tunnel] start failed: %v", err)
		} else {
			_ = os.WriteFile(pidFile, []byte(strconv.Itoa(cmd.Process.Pid)), 0o644)
			log.Printf("[tunnel] cloudflared started (mode %s) -> %s", t.Mode, local)
			go g.watchTunnelLog(out)
			exited := make(chan error, 1)
			go func() { exited <- cmd.Wait() }()
			select {
			case err := <-exited:
				log.Printf("[tunnel] cloudflared exited: %v", err)
			case <-ctx.Done():
				_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM)
				select {
				case <-exited:
				case <-time.After(5 * time.Second):
					_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
				}
				return
			}
		}
		if time.Since(started) > time.Minute {
			backoff = 2 * time.Second
		}
		log.Printf("[tunnel] reconnecting in %s", backoff)
		select {
		case <-ctx.Done():
			return
		case <-time.After(backoff):
		}
		if backoff < time.Minute {
			backoff *= 2
		}
	}
}

func (g *Gateway) watchTunnelLog(r interface{ Read([]byte) (int, error) }) {
	sc := bufio.NewScanner(r)
	conns := 0
	for sc.Scan() {
		line := sc.Text()
		if strings.Contains(line, "Registered tunnel connection") {
			conns++ // cloudflared opens 4 connections; announce the first only
			if conns > 1 {
				continue
			}
		}
		switch {
		case quickURL.MatchString(line):
			u := quickURL.FindString(line)
			log.Printf("[tunnel] ==================================================")
			log.Printf("[tunnel] PUBLIC URL: %s", u)
			log.Printf("[tunnel] (quick mode: this URL changes every restart)")
			log.Printf("[tunnel] ==================================================")
		case strings.Contains(line, "Registered tunnel connection"):
			msg := "connected to Cloudflare"
			if g.cfg.Tunnel.PublicURL != "" {
				msg += " — public URL: " + g.cfg.Tunnel.PublicURL
			}
			log.Printf("[tunnel] %s", msg)
		case strings.Contains(line, " ERR ") || strings.Contains(line, "error"):
			log.Printf("[tunnel] %s", line)
		}
	}
}

// killStale stops a cloudflared left running by a gateway that was killed
// without cleaning up (otherwise it would keep serving next to the new one).
func killStale(pidFile string) {
	b, err := os.ReadFile(pidFile)
	if err != nil {
		return
	}
	pid, err := strconv.Atoi(strings.TrimSpace(string(b)))
	if err != nil || pid <= 1 {
		return
	}
	out, err := exec.Command("ps", "-p", strconv.Itoa(pid), "-o", "command=").Output()
	if err != nil || !strings.Contains(string(out), "cloudflared") {
		return // gone, or the pid now belongs to something else
	}
	log.Printf("[tunnel] stopping stale cloudflared (pid %d) from a previous run", pid)
	_ = syscall.Kill(-pid, syscall.SIGTERM)
	_ = syscall.Kill(pid, syscall.SIGTERM)
	time.Sleep(time.Second)
}

// localAddr turns a listen address like 0.0.0.0:8080 or :8080 into 127.0.0.1:8080.
func localAddr(listen string) string {
	i := strings.LastIndex(listen, ":")
	if i < 0 {
		return listen
	}
	host, port := listen[:i], listen[i+1:]
	if host == "" || host == "0.0.0.0" || host == "[::]" {
		host = "127.0.0.1"
	}
	return fmt.Sprintf("%s:%s", host, port)
}
