package server

import (
	"sync"
	"time"
)

// limiter counts requests per key per minute, and failed sign-ins per address with a wait
// that doubles after each failure (1 s, 2 s, 4 s … up to 15 minutes).
type limiter struct {
	mu     sync.Mutex
	window map[string]*count
	fails  map[string]*failure
	last   time.Time
}

type count struct {
	start time.Time
	n     int
}

type failure struct {
	n     int
	until time.Time
}

func newLimiter() *limiter {
	return &limiter{window: map[string]*count{}, fails: map[string]*failure{}}
}

// allow reports whether one more request under key fits in perMin per minute.
func (l *limiter) allow(key string, perMin int) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := time.Now()
	l.sweep(now)
	c := l.window[key]
	if c == nil || now.Sub(c.start) >= time.Minute {
		c = &count{start: now}
		l.window[key] = c
	}
	c.n++
	return c.n <= perMin
}

// loginWait is how long this address must wait before trying to sign in again.
func (l *limiter) loginWait(ip string) time.Duration {
	l.mu.Lock()
	defer l.mu.Unlock()
	if f := l.fails[ip]; f != nil {
		if d := time.Until(f.until); d > 0 {
			return d
		}
	}
	return 0
}

func (l *limiter) loginFailed(ip string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	f := l.fails[ip]
	if f == nil {
		f = &failure{}
		l.fails[ip] = f
	}
	f.n++
	wait := time.Second << min(f.n-1, 10)
	if wait > 15*time.Minute {
		wait = 15 * time.Minute
	}
	f.until = time.Now().Add(wait)
}

func (l *limiter) loginOK(ip string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	delete(l.fails, ip)
}

// sweep drops old entries now and then, so the maps stay small.
func (l *limiter) sweep(now time.Time) {
	if now.Sub(l.last) < 5*time.Minute {
		return
	}
	l.last = now
	for k, c := range l.window {
		if now.Sub(c.start) >= time.Minute {
			delete(l.window, k)
		}
	}
	for k, f := range l.fails {
		if now.Sub(f.until) > time.Hour {
			delete(l.fails, k)
		}
	}
}
