// Workflow delete_assist_memory, node work (imported by tools/importer).
package work

import "espos/internal/wm"

// POST /v1/assist/memories/delete
//   {"id": 12}        forget one memory (it must be the caller's)
//   {"all": true}     forget every memory
//   {"chats": true}   forget the recent conversations (on every device): their words and summaries
//   {"session": 34}   forget one conversation (it must be the caller's)
// Answers {deleted: n}.

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	body, _ := in["body"].(map[string]any)
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	id := wm.Int(body["id"])
	all := wm.Bool(body["all"])
	chats := wm.Bool(body["chats"])
	session := wm.Int(body["session"])
	if id <= 0 && !all && !chats && session <= 0 {
		return map[string]any{"status": 422, "body": map[string]any{"error": "nothing_to_delete"}}
	}
	mf := []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}
	if id > 0 {
		mf = append(mf, map[string]any{"column": "id", "op": "eq", "value": id})
	}
	qs := []map[string]any{}
	if id > 0 || all {
		qs = append(qs, map[string]any{"table": "assist_memories", "filters": mf, "limit": 200, "with_total": false})
	}
	cf := []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}
	if session > 0 && !chats {
		cf = append(cf, map[string]any{"column": "id", "op": "eq", "value": session})
	}
	if chats || session > 0 {
		qs = append(qs, map[string]any{"table": "assist_convos", "filters": cf, "limit": 200, "with_total": false})
	}
	res, err := wm.QueryMany(qs)
	if err != nil {
		wm.Log("read: ", err.Error())
		return map[string]any{"status": 500, "body": map[string]any{"error": "server_error"}}
	}
	ops := []map[string]any{}
	i := 0
	if id > 0 || all {
		for _, m := range rowsOf(res, i) {
			if wm.Int(m["user_id"]) == uid && len(ops) < 100 {
				ops = append(ops, map[string]any{"op": "delete", "table": "assist_memories", "id": wm.Int(m["id"])})
			}
		}
		i++
	}
	if chats || session > 0 {
		for _, m := range rowsOf(res, i) {
			if wm.Int(m["user_id"]) == uid && len(ops) < 100 {
				ops = append(ops, map[string]any{"op": "delete", "table": "assist_convos", "id": wm.Int(m["id"])})
			}
		}
	}
	if (id > 0 || session > 0) && len(ops) == 0 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "not_found"}}
	}
	if len(ops) > 0 {
		if _, berr := wm.Batch(ops); berr != nil {
			wm.Log("delete: ", berr.Error())
			return map[string]any{"status": 500, "body": map[string]any{"error": "server_error"}}
		}
	}
	return map[string]any{"status": 200, "body": map[string]any{"deleted": len(ops)}}
}

func rowsOf(res []map[string]any, i int) []map[string]any {
	if i >= len(res) {
		return nil
	}
	switch r := res[i]["rows"].(type) {
	case []map[string]any:
		return r
	case []any:
		out := []map[string]any{}
		for _, x := range r {
			if m, ok := x.(map[string]any); ok {
				out = append(out, m)
			}
		}
		return out
	}
	return nil
}
