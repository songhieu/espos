package engine

import (
	"context"
	"log"
	"strconv"
	"strings"
	"time"
)

// RunSchedules runs every flow with a schedule_trigger when its 5-field cron expression
// matches the minute (UTC), until ctx ends.
func (e *Engine) RunSchedules(ctx context.Context) {
	for {
		now := time.Now().UTC()
		next := now.Truncate(time.Minute).Add(time.Minute)
		select {
		case <-ctx.Done():
			return
		case <-time.After(time.Until(next)):
		}
		for code, f := range e.Flows {
			if f.Cron == "" || !cronMatch(f.Cron, next) {
				continue
			}
			if e.ScheduleGate != nil && !e.ScheduleGate(code) {
				continue
			}
			go func(code string) {
				if _, err := e.Run(code, map[string]any{}, nil, ""); err != nil {
					log.Printf("schedule %s: %v", code, err)
				}
			}(code)
		}
	}
}

func cronMatch(expr string, t time.Time) bool {
	f := strings.Fields(expr)
	if len(f) != 5 {
		return false
	}
	return field(f[0], t.Minute(), 0, 59) && field(f[1], t.Hour(), 0, 23) && field(f[2], t.Day(), 1, 31) &&
		field(f[3], int(t.Month()), 1, 12) && field(f[4], int(t.Weekday()), 0, 6)
}

// field matches one cron field: *, */n, a, a-b, a-b/n and comma lists of these.
func field(spec string, v, lo, hi int) bool {
	for _, part := range strings.Split(spec, ",") {
		step := 1
		if i := strings.Index(part, "/"); i >= 0 {
			s, err := strconv.Atoi(part[i+1:])
			if err != nil || s <= 0 {
				continue
			}
			step = s
			part = part[:i]
		}
		from, to := lo, hi
		if part != "*" {
			if i := strings.Index(part, "-"); i >= 0 {
				a, err1 := strconv.Atoi(part[:i])
				b, err2 := strconv.Atoi(part[i+1:])
				if err1 != nil || err2 != nil {
					continue
				}
				from, to = a, b
			} else {
				a, err := strconv.Atoi(part)
				if err != nil {
					continue
				}
				from, to = a, a
				if step > 1 {
					to = hi
				}
			}
		}
		if v >= from && v <= to && (v-from)%step == 0 {
			return true
		}
	}
	return false
}
