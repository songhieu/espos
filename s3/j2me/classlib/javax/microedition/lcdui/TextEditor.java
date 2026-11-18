package javax.microedition.lcdui;

/**
 * Text input with a phone keypad, shared by TextField and TextBox. Not a MIDP class.
 * Multi-tap: 2 abc, 3 def, 4 ghi, 5 jkl, 6 mno, 7 pqrs, 8 tuv, 9 wxyz (the key's digit last), 0 space then 0, 1
 * punctuation; the same key again within TAP_MS changes the letter just typed, holding a key types its digit.
 * # switches abc / ABC / 123 (NUMERIC: the sign; DECIMAL: the point, then the sign; PHONENUMBER: +), * deletes the
 * character before the caret, left / right move the caret. maxSize and the constraints are kept at all times.
 */
final class TextEditor {
    static final int TAP_MS = 900;
    private static final String[] KEYS = {" 0", ".,?!'\"-()@/:_;+&%*=<>#1", "abc2", "def3", "ghi4", "jkl5", "mno6",
            "pqrs7", "tuv8", "wxyz9"};
    static final int LOWER = 0, UPPER = 1, DIGITS = 2;
    private static final String[] MODE_NAMES = {"abc", "ABC", "123"};

    final StringBuffer buf = new StringBuffer();
    int caret, max, constraints;
    int mode = LOWER;
    Screen screen;  // repainted when a multi-tap pause ends
    // the letter being multi-tapped: at caret - 1
    private int tapKey = -1, tapIndex;
    private long tapTime;
    // the last layout (for touches and up / down)
    private int[] starts = {0};
    private int lineCount = 1;
    private Font lastFont;

    TextEditor(String text, int maxSize, int constraints) {
        if (maxSize <= 0) throw new IllegalArgumentException();
        checkConstraints(constraints);
        max = maxSize;
        this.constraints = constraints;
        set(text);
    }

    // ---- the constraints
    int kind() { return constraints & TextField.CONSTRAINT_MASK; }

    boolean editable() { return (constraints & TextField.UNEDITABLE) == 0; }

    boolean password() { return (constraints & TextField.PASSWORD) != 0; }

    private boolean numbers() {
        int k = kind();
        return k == TextField.NUMERIC || k == TextField.DECIMAL || k == TextField.PHONENUMBER;
    }

    static void checkConstraints(int c) {
        int k = c & TextField.CONSTRAINT_MASK;
        if (k < TextField.ANY || k > TextField.DECIMAL) throw new IllegalArgumentException();
    }

    /** Whether s may be the content under kind k. */
    static boolean valid(String s, int k) {
        int n = s.length();
        if (k == TextField.NUMERIC || k == TextField.DECIMAL) {
            int i = 0, dots = 0;
            if (n > 0 && s.charAt(0) == '-') i = 1;
            for (; i < n; i++) {
                char c = s.charAt(i);
                if (c == '.' && k == TextField.DECIMAL && dots++ == 0) continue;
                if (c < '0' || c > '9') return false;
            }
            return true;
        }
        if (k == TextField.PHONENUMBER) {
            for (int i = 0; i < n; i++) if ("0123456789+*#pw ()-".indexOf(s.charAt(i)) < 0) return false;
        }
        return true;
    }

    // ---- the content
    String get() { return buf.toString(); }

    int size() { return buf.length(); }

    void set(String text) {
        if (text == null) text = "";
        if (text.length() > max) throw new IllegalArgumentException("longer than maxSize");
        if (!valid(text, kind())) throw new IllegalArgumentException("not allowed by the constraints");
        buf.setLength(0);
        buf.append(text);
        caret = buf.length();
        tapKey = -1;
    }

    void insert(String s, int pos) {
        if (s == null) throw new NullPointerException();
        if (pos < 0) pos = 0;
        if (pos > buf.length()) pos = buf.length();
        String t = buf.toString();
        String next = t.substring(0, pos) + s + t.substring(pos);
        if (next.length() > max) throw new IllegalArgumentException("longer than maxSize");
        if (!valid(next, kind())) throw new IllegalArgumentException("not allowed by the constraints");
        buf.insert(pos, s);
        if (caret >= pos) caret += s.length();
        tapKey = -1;
    }

    void delete(int off, int len) {
        if (off < 0 || len < 0 || off + len > buf.length()) throw new StringIndexOutOfBoundsException();
        String t = buf.toString();
        String next = t.substring(0, off) + t.substring(off + len);
        if (!valid(next, kind())) throw new IllegalArgumentException("not allowed by the constraints");
        buf.delete(off, off + len);
        if (caret > off + len) caret -= len;
        else if (caret > off) caret = off;
        tapKey = -1;
    }

    int setMax(int m) {
        if (m <= 0) throw new IllegalArgumentException();
        max = m;
        if (buf.length() > m) {
            String t = buf.toString().substring(0, m);
            if (!valid(t, kind())) t = "";
            buf.setLength(0);
            buf.append(t);
        }
        if (caret > buf.length()) caret = buf.length();
        return max;
    }

    void setConstraints(int c) {
        checkConstraints(c);
        constraints = c;
        if (!valid(buf.toString(), kind())) {
            buf.setLength(0);
            caret = 0;
        }
        tapKey = -1;
        if (numbers()) mode = DIGITS;
        else if (mode == DIGITS) mode = LOWER;
    }

    // ---- keys
    private boolean tapping(long now) { return tapKey >= 0 && now - tapTime < TAP_MS && caret > 0; }

    private void put(char c) {
        if (buf.length() >= max) return;
        String t = buf.toString();
        if (!valid(t.substring(0, caret) + c + t.substring(caret), kind())) return;
        buf.insert(caret, c);
        caret++;
    }

    private char cased(char c) { return mode == UPPER ? Character.toUpperCase(c) : c; }

    /** A key (1 pressed, 2 released, 3 repeated); true when it was used. Up and down are not used here. */
    boolean key(int type, int code) {
        if (type == 2) return false;
        long now = System.currentTimeMillis();
        if (code == Canvas.K_LEFT || code == Canvas.K_RIGHT) {
            tapKey = -1;
            if (code == Canvas.K_LEFT && caret > 0) caret--;
            else if (code == Canvas.K_RIGHT && caret < buf.length()) caret++;
            return true;
        }
        if (!editable()) return false;
        if (code == Canvas.KEY_STAR) {
            tapKey = -1;
            if (caret > 0) {
                String t = buf.toString();
                if (valid(t.substring(0, caret - 1) + t.substring(caret), kind())) {
                    buf.deleteCharAt(caret - 1);
                    caret--;
                }
            }
            return true;
        }
        if (code == Canvas.KEY_POUND) {
            if (type != 1) return true;
            tapKey = -1;
            pound();
            return true;
        }
        if (code < Canvas.KEY_NUM0 || code > Canvas.KEY_NUM9) return false;
        int d = code - Canvas.KEY_NUM0;
        if (type == 3) {  // held: the digit itself, in place of the letter
            if (tapKey == code && caret > 0 && !numbers() && mode != DIGITS) {
                buf.setCharAt(caret - 1, (char) code);
                tapKey = -1;
            }
            return true;
        }
        if (numbers() || mode == DIGITS) {
            tapKey = -1;
            put((char) code);
            return true;
        }
        String keys = KEYS[d];
        if (tapKey == code && tapping(now)) {
            tapIndex = (tapIndex + 1) % keys.length();
            buf.setCharAt(caret - 1, cased(keys.charAt(tapIndex)));
        } else {
            int before = buf.length();
            put(cased(keys.charAt(0)));
            if (buf.length() == before) {
                tapKey = -1;
                return true;
            }
            tapIndex = 0;
        }
        tapKey = code;
        tapTime = now;
        if (screen != null) Ticks.later(screen, TAP_MS + 20);
        return true;
    }

    private void pound() {
        int k = kind();
        String t = buf.toString();
        if (k == TextField.NUMERIC || (k == TextField.DECIMAL && t.indexOf('.') >= 0)) {
            if (t.startsWith("-")) {
                buf.deleteCharAt(0);
                if (caret > 0) caret--;
            } else if (buf.length() < max) {
                buf.insert(0, '-');
                caret++;
            }
        } else if (k == TextField.DECIMAL) {
            put('.');
        } else if (k == TextField.PHONENUMBER) {
            put('+');
        } else {
            mode = (mode + 1) % 3;
        }
    }

    /** The input mode's tag ("abc", "ABC", "123"), or null when the text cannot be edited. */
    String modeName() {
        if (!editable()) return null;
        return numbers() ? "123" : MODE_NAMES[mode];
    }

    // ---- layout: lines with the offsets of their first characters (so the caret maps)
    String shown() {
        if (!password()) return buf.toString();
        StringBuffer s = new StringBuffer();
        long now = System.currentTimeMillis();
        for (int i = 0; i < buf.length(); i++) s.append(i == caret - 1 && tapping(now) ? buf.charAt(i) : '*');
        return s.toString();
    }

    /** Line starts of s at width w; lineCount of them. A line keeps its trailing spaces and its newline. */
    private void layout(String s, Font f, int w) {
        lastFont = f;
        int n = s.length(), k = 0;
        int[] out = starts.length >= 8 ? starts : new int[8];
        out[k++] = 0;
        int a = 0;
        while (a < n) {
            int nl = s.indexOf('\n', a);
            int end = nl < 0 ? n : nl;
            int b = end;
            if (end > a && f.substringWidth(s, a, end - a) > w) {
                int lo = a + 1, hi = end;  // the most characters that fit (one at least)
                while (lo < hi) {
                    int mid = (lo + hi + 1) >>> 1;
                    if (f.substringWidth(s, a, mid - a) <= w) lo = mid;
                    else hi = mid - 1;
                }
                b = lo;
                if (b < end && s.charAt(b) != ' ') {
                    int sp = s.lastIndexOf(' ', b - 1);
                    if (sp >= a) b = sp + 1;
                }
                while (b < end && s.charAt(b) == ' ') b++;
            }
            if (b == end && nl >= 0) b = end + 1;  // the newline ends this line
            if (b >= n && !(n > 0 && s.charAt(n - 1) == '\n')) break;
            if (k == out.length) {
                int[] o2 = new int[k * 2];
                System.arraycopy(out, 0, o2, 0, k);
                out = o2;
            }
            out[k++] = b;
            a = b;
        }
        starts = out;
        lineCount = k;
    }

    private int lineEnd(String s, int i) {
        int e = i + 1 < lineCount ? starts[i + 1] : s.length();
        if (e > starts[i] && s.charAt(e - 1) == '\n') e--;
        return e;
    }

    private int caretLine() {
        int l = 0;
        while (l + 1 < lineCount && starts[l + 1] <= caret) l++;
        return l;
    }

    /** The text's height at width w (one line at least). */
    int height(Font f, int w) {
        layout(shown(), f, w);
        return lineCount * f.getHeight();
    }

    /** The caret's line top (from the text's top), after a height or paint at the same width. */
    int caretY() { return caretLine() * (lastFont != null ? lastFont.getHeight() : 0); }

    /** Draws the text from x, y in w; the caret when focused; the letter being tapped on a tint. */
    void paint(Graphics g, Font f, int x, int y, int w, boolean focused) {
        String s = shown();
        layout(s, f, w);
        int fh = f.getHeight(), cl = caretLine();
        boolean tap = focused && tapping(System.currentTimeMillis());
        g.setFont(f);
        for (int i = 0; i < lineCount; i++) {
            int a = starts[i], e = lineEnd(s, i), ly = y + i * fh;
            if (tap && caret - 1 >= a && caret - 1 < e) {
                int tx = x + f.substringWidth(s, a, caret - 1 - a);
                g.setColor(Ui.TINT);
                g.fillRect(tx, ly, f.substringWidth(s, caret - 1, 1), fh);
            }
            g.setColor(editable() ? Screen.INK : Screen.MUTED);
            if (e > a) g.drawSubstring(s, a, e - a, x, ly, Graphics.LEFT | Graphics.TOP);
            if (focused && i == cl) {
                int cx = x + f.substringWidth(s, a, Math.min(caret, e) - a);
                g.setColor(Screen.ACCENT);
                g.fillRect(cx, ly + 1, 2, fh - 2);
            }
        }
    }

    /** Moves the caret to the line above (-1) or below (1), near the same x; false at the first / last line. */
    boolean lineMove(int dir) {
        if (lastFont == null) return false;
        String s = shown();
        int l = caretLine(), t = l + dir;
        if (t < 0 || t >= lineCount) return false;
        int x = lastFont.substringWidth(s, starts[l], caret - starts[l]);
        caret = charAtX(s, t, x);
        tapKey = -1;
        return true;
    }

    private int charAtX(String s, int line, int x) {
        int a = starts[line], e = lineEnd(s, line), best = a;
        for (int i = a; i <= e; i++) {
            int cw = lastFont.substringWidth(s, a, i - a);
            if (cw <= x) best = i;
            if (cw >= x) {
                if (cw - x < x - lastFont.substringWidth(s, a, best - a)) best = i;
                break;
            }
        }
        return best;
    }

    /** A touch at x, y from the text's top-left: the caret goes there. */
    void touch(int x, int y) {
        if (lastFont == null) return;
        int l = y / lastFont.getHeight();
        if (l < 0) l = 0;
        if (l >= lineCount) l = lineCount - 1;
        caret = charAtX(shown(), l, x);
        tapKey = -1;
    }
}
