package wm

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"regexp"
	"strings"
	"sync"
	"time"

	_ "modernc.org/sqlite" // pure-Go SQLite driver
)

// Every table is a set of JSON documents in one SQLite table ("docs"). A single user's
// data is small, and keeping the platform's document model means the node code needs no
// SQL of its own.

// Column is one declared column of a table (internal/backend/tables/*.json).
type Column struct {
	Code    string `json:"code"`
	Type    string `json:"type"`
	Default any    `json:"default"`
	Indexed bool   `json:"indexed"`
	Unique  bool   `json:"unique"`
}

// Table is a table declaration.
type Table struct {
	Code    string   `json:"code"`
	Columns []Column `json:"columns"`
}

var (
	db      *sql.DB
	tables  = map[string]Table{}
	writeMu sync.Mutex // read-modify-write updates and upserts
)

var columnName = regexp.MustCompile(`^[a-z][a-z0-9_]{0,62}$`)

// Open opens (or creates) the database file and declares the tables.
func Open(path string, decls []Table) error {
	d, err := sql.Open("sqlite", "file:"+path+"?_pragma=journal_mode(WAL)&_pragma=busy_timeout(5000)&_pragma=foreign_keys(on)")
	if err != nil {
		return err
	}
	d.SetMaxOpenConns(1) // SQLite writes one at a time; one connection avoids "database is locked"
	if _, err := d.Exec(`CREATE TABLE IF NOT EXISTS docs (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		tbl TEXT NOT NULL,
		created_at TEXT NOT NULL,
		updated_at TEXT NOT NULL,
		data TEXT NOT NULL)`); err != nil {
		return err
	}
	if _, err := d.Exec(`CREATE INDEX IF NOT EXISTS docs_tbl ON docs(tbl, id)`); err != nil {
		return err
	}
	for _, t := range decls {
		tables[t.Code] = t
		for _, c := range t.Columns {
			if !(c.Indexed || c.Unique) || !columnName.MatchString(c.Code) || !columnName.MatchString(t.Code) {
				continue
			}
			kind := "INDEX"
			if c.Unique {
				kind = "UNIQUE INDEX" // NULLs do not collide
			}
			q := fmt.Sprintf(`CREATE %s IF NOT EXISTS docs_%s_%s ON docs(tbl, json_extract(data, '$.%s')) WHERE tbl = '%s'`,
				kind, t.Code, c.Code, c.Code, t.Code)
			if _, err := d.Exec(q); err != nil {
				return err
			}
		}
	}
	db = d
	for _, f := range []string{path, path + "-wal", path + "-shm"} {
		_ = os.Chmod(f, 0o600) // the database holds session and device token hashes
	}
	return nil
}

// Close closes the database.
func Close() error {
	if db == nil {
		return nil
	}
	return db.Close()
}

func nowStamp() string { return time.Now().UTC().Format(time.RFC3339) }

func rowOf(id int64, created, updated, data string) (map[string]any, error) {
	m := map[string]any{}
	if err := json.Unmarshal([]byte(data), &m); err != nil {
		return nil, err
	}
	m["id"] = id
	m["created_at"] = created
	m["updated_at"] = updated
	return m, nil
}

func encode(m map[string]any) (string, error) {
	clean := make(map[string]any, len(m))
	for k, v := range m {
		if k == "id" || k == "created_at" || k == "updated_at" || k == "deleted_at" {
			continue
		}
		clean[k] = v
	}
	b, err := json.Marshal(clean)
	return string(b), err
}

func columnExpr(col string) (string, error) {
	switch col {
	case "id", "created_at", "updated_at":
		return col, nil
	}
	if !columnName.MatchString(col) {
		return "", fmt.Errorf("bad column %q", col)
	}
	return "json_extract(data, '$." + col + "')", nil
}

// sqlValue maps a Go value onto what json_extract returns for it.
func sqlValue(v any) any {
	switch x := v.(type) {
	case bool:
		if x {
			return 1
		}
		return 0
	case int:
		return int64(x)
	case float64:
		if x == float64(int64(x)) {
			return int64(x)
		}
		return x
	}
	return v
}

func asFilters(v any) []map[string]any {
	switch x := v.(type) {
	case []map[string]any:
		return x
	case []any:
		out := make([]map[string]any, 0, len(x))
		for _, it := range x {
			if m, ok := it.(map[string]any); ok {
				out = append(out, m)
			}
		}
		return out
	}
	return nil
}

// coerce gives a filter value its column's declared type, so "123" finds the number 123.
func coerce(table, col string, v any) any {
	if v == nil {
		return nil
	}
	typ := ""
	if col == "id" {
		typ = "integer"
	} else if t, ok := tables[table]; ok {
		for _, c := range t.Columns {
			if c.Code == col {
				typ = c.Type
			}
		}
	}
	conv := func(x any) any {
		switch typ {
		case "integer", "number", "reference", "owner":
			if s, ok := x.(string); ok {
				return Num(s)
			}
		case "boolean":
			return Bool(x)
		case "text", "long_text", "email", "url", "uuid", "enum", "date", "datetime":
			if _, ok := x.(string); !ok {
				return Str(x)
			}
		}
		return x
	}
	if list, ok := v.([]any); ok {
		out := make([]any, len(list))
		for i, x := range list {
			out[i] = conv(x)
		}
		return out
	}
	return conv(v)
}

func where(table string, filters []map[string]any, match string) (string, []any, error) {
	conds := []string{}
	args := []any{}
	for _, f := range filters {
		expr, err := columnExpr(Str(f["column"]))
		if err != nil {
			return "", nil, err
		}
		op := Str(f["op"])
		if op == "" {
			op = "eq"
		}
		val := coerce(table, Str(f["column"]), f["value"])
		switch op {
		case "eq":
			if val == nil {
				conds = append(conds, expr+" IS NULL")
			} else {
				conds = append(conds, expr+" = ?")
				args = append(args, sqlValue(val))
			}
		case "ne":
			if val == nil {
				conds = append(conds, expr+" IS NOT NULL")
			} else {
				conds = append(conds, "("+expr+" IS NULL OR "+expr+" != ?)")
				args = append(args, sqlValue(val))
			}
		case "gt", "gte", "lt", "lte":
			sym := map[string]string{"gt": ">", "gte": ">=", "lt": "<", "lte": "<="}[op]
			conds = append(conds, expr+" "+sym+" ?")
			args = append(args, sqlValue(val))
		case "contains":
			conds = append(conds, "instr(lower("+expr+"), lower(?)) > 0")
			args = append(args, Str(val))
		case "starts":
			conds = append(conds, "substr("+expr+", 1, length(?)) = ?")
			args = append(args, Str(val), Str(val))
		case "in":
			list, _ := val.([]any)
			if l2, ok := val.([]string); ok {
				for _, s := range l2 {
					list = append(list, s)
				}
			}
			if l3, ok := val.([]int); ok {
				for _, n := range l3 {
					list = append(list, n)
				}
			}
			if len(list) == 0 {
				conds = append(conds, "0")
				continue
			}
			marks := strings.TrimSuffix(strings.Repeat("?,", len(list)), ",")
			conds = append(conds, expr+" IN ("+marks+")")
			for _, it := range list {
				args = append(args, sqlValue(it))
			}
		case "is_null":
			conds = append(conds, expr+" IS NULL")
		case "not_null":
			conds = append(conds, expr+" IS NOT NULL")
		default:
			return "", nil, fmt.Errorf("unknown filter op %q", op)
		}
	}
	sqlWhere := "tbl = ?"
	allArgs := []any{table}
	if len(conds) > 0 {
		join := " AND "
		if match == "any" {
			join = " OR "
		}
		sqlWhere += " AND (" + strings.Join(conds, join) + ")"
		allArgs = append(allArgs, args...)
	}
	return sqlWhere, allArgs, nil
}

func checkTable(table string) error {
	if !columnName.MatchString(table) {
		return fmt.Errorf("bad table %q", table)
	}
	return nil
}

// QueryOpts reads rows: filters ({column, op, value}), match ("all"/"any"), order_by, desc,
// limit (default 100) and offset.
func QueryOpts(table string, opts map[string]any) ([]map[string]any, error) {
	rows, _, err := QueryPage(table, opts)
	return rows, err
}

// QueryPage is QueryOpts plus the number of matching rows.
func QueryPage(table string, opts map[string]any) ([]map[string]any, int, error) {
	if err := checkTable(table); err != nil {
		return nil, 0, err
	}
	if opts == nil {
		opts = map[string]any{}
	}
	w, args, err := where(table, asFilters(opts["filters"]), Str(opts["match"]))
	if err != nil {
		return nil, 0, err
	}
	order := "id"
	if ob := Str(opts["order_by"]); ob != "" {
		if order, err = columnExpr(ob); err != nil {
			return nil, 0, err
		}
	}
	dir := "ASC"
	if Bool(opts["desc"]) {
		dir = "DESC"
	}
	limit := Int(opts["limit"])
	if limit <= 0 {
		limit = 100
	}
	if limit > 5000 {
		limit = 5000
	}
	offset := Int(opts["offset"])
	q := fmt.Sprintf("SELECT id, created_at, updated_at, data FROM docs WHERE %s ORDER BY %s %s, id %s LIMIT %d OFFSET %d",
		w, order, dir, dir, limit, offset)
	rs, err := db.Query(q, args...)
	if err != nil {
		return nil, 0, err
	}
	defer rs.Close()
	out := []map[string]any{}
	for rs.Next() {
		var id int64
		var c, u, d string
		if err := rs.Scan(&id, &c, &u, &d); err != nil {
			return nil, 0, err
		}
		m, err := rowOf(id, c, u, d)
		if err != nil {
			return nil, 0, err
		}
		out = append(out, m)
	}
	if err := rs.Err(); err != nil {
		return nil, 0, err
	}
	total := len(out)
	if len(out) == limit || offset > 0 {
		if err := db.QueryRow("SELECT count(*) FROM docs WHERE "+w, args...).Scan(&total); err != nil {
			return nil, 0, err
		}
	}
	return out, total, nil
}

// Query reads rows matching filters.
func Query(table string, filters []map[string]any) ([]map[string]any, error) {
	return QueryOpts(table, map[string]any{"filters": filters, "limit": 5000})
}

// Get reads one row by id (nil when there is none).
func Get(table string, id any) (map[string]any, error) {
	var c, u, d string
	var rid int64
	err := db.QueryRow("SELECT id, created_at, updated_at, data FROM docs WHERE tbl = ? AND id = ?", table, int64(Num(id))).Scan(&rid, &c, &u, &d)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return rowOf(rid, c, u, d)
}

func withDefaults(table string, values map[string]any) map[string]any {
	out := make(map[string]any, len(values))
	for k, v := range values {
		out[k] = v
	}
	if t, ok := tables[table]; ok {
		for _, c := range t.Columns {
			if _, set := out[c.Code]; !set && c.Default != nil {
				out[c.Code] = c.Default
			}
		}
	}
	return out
}

// Insert adds a row (declared column defaults fill what is missing) and returns it.
func Insert(table string, values map[string]any) (map[string]any, error) {
	if err := checkTable(table); err != nil {
		return nil, err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	return insertLocked(table, values)
}

func insertLocked(table string, values map[string]any) (map[string]any, error) {
	full := withDefaults(table, values)
	data, err := encode(full)
	if err != nil {
		return nil, err
	}
	now := nowStamp()
	res, err := db.Exec("INSERT INTO docs (tbl, created_at, updated_at, data) VALUES (?, ?, ?, ?)", table, now, now, data)
	if err != nil {
		return nil, err
	}
	id, err := res.LastInsertId()
	if err != nil {
		return nil, err
	}
	return rowOf(id, now, now, data)
}

// Update merges values into the row and returns it.
func Update(table string, id any, values map[string]any) (map[string]any, error) {
	writeMu.Lock()
	defer writeMu.Unlock()
	return updateLocked(table, id, values)
}

func updateLocked(table string, id any, values map[string]any) (map[string]any, error) {
	cur, err := Get(table, id)
	if err != nil {
		return nil, err
	}
	if cur == nil {
		return nil, fmt.Errorf("%s %v: not found", table, id)
	}
	for k, v := range values {
		cur[k] = v
	}
	data, err := encode(cur)
	if err != nil {
		return nil, err
	}
	now := nowStamp()
	if _, err := db.Exec("UPDATE docs SET data = ?, updated_at = ? WHERE tbl = ? AND id = ?", data, now, table, int64(Num(id))); err != nil {
		return nil, err
	}
	return rowOf(int64(Num(id)), Str(cur["created_at"]), now, data)
}

// UpdateWith is Update with options (accepted for compatibility; none are used here).
func UpdateWith(table string, id any, values map[string]any, _ map[string]any) (map[string]any, error) {
	return Update(table, id, values)
}

// Upsert updates the row whose `on` columns equal values' (merging values in) or inserts one.
func Upsert(table string, on []string, values map[string]any, _ map[string]any) (map[string]any, error) {
	if err := checkTable(table); err != nil {
		return nil, err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	filters := make([]map[string]any, 0, len(on))
	for _, c := range on {
		filters = append(filters, map[string]any{"column": c, "op": "eq", "value": values[c]})
	}
	rows, _, err := QueryPage(table, map[string]any{"filters": filters, "limit": 1})
	if err != nil {
		return nil, err
	}
	if len(rows) == 1 {
		return updateLocked(table, rows[0]["id"], values)
	}
	return insertLocked(table, values)
}

// Delete removes a row.
func Delete(table string, id any) error {
	writeMu.Lock()
	defer writeMu.Unlock()
	_, err := db.Exec("DELETE FROM docs WHERE tbl = ? AND id = ?", table, int64(Num(id)))
	return err
}
