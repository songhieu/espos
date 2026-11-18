package javax.microedition.lcdui;

import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

/**
 * A date and / or time shown as dd/MM/yyyy HH:mm ("--" while unset). Editing: OK (or a tap) starts it; then left /
 * right pick the part, up / down (or the digits 2 / 8) change it, OK ends. Out of editing, up / down move on.
 */
public class DateField extends Item {
    public static final int DATE = 1, TIME = 2, DATE_TIME = 3;

    private static final int DAY = 0, MONTH = 1, YEAR = 2, HOUR = 3, MINUTE = 4;

    private final TimeZone zone;
    private int mode;
    private Date date;        // null: not set
    private int part;         // the part being edited (index into parts())
    private boolean editing;

    public DateField(String label, int mode) { this(label, mode, null); }

    public DateField(String label, int mode, TimeZone timeZone) {
        super(label);
        checkMode(mode);
        this.mode = mode;
        zone = timeZone != null ? timeZone : TimeZone.getDefault();
    }

    private static void checkMode(int m) {
        if (m < DATE || m > DATE_TIME) throw new IllegalArgumentException();
    }

    public Date getDate() { return date == null ? null : new Date(date.getTime()); }

    public void setDate(Date date) {
        this.date = date == null ? null : normalize(date.getTime());
        changed();
    }

    public int getInputMode() { return mode; }

    public void setInputMode(int mode) {
        checkMode(mode);
        this.mode = mode;
        part = 0;
        if (date != null) date = normalize(date.getTime());
        changed();
    }

    // ---- the parts
    private Calendar cal(long t) {
        Calendar c = Calendar.getInstance(zone);
        c.setTime(new Date(t));
        return c;
    }

    /** DATE: the day at 00:00; TIME: the time of day on 1 January 1970 (as MIDP asks). */
    private Date normalize(long t) {
        Calendar c = cal(t);
        int y = c.get(Calendar.YEAR), mo = c.get(Calendar.MONTH), d = c.get(Calendar.DATE);
        int h = c.get(Calendar.HOUR_OF_DAY), mi = c.get(Calendar.MINUTE);
        if (mode == DATE) h = mi = 0;
        if (mode == TIME) {
            y = 1970;
            mo = 0;
            d = 1;
        }
        return make(y, mo, d, h, mi);
    }

    private Date make(int y, int mo, int d, int h, int mi) {
        Calendar c = Calendar.getInstance(zone);
        c.set(Calendar.DATE, 1);  // first, so that setting the month never rolls a 31st over
        c.set(Calendar.YEAR, y);
        c.set(Calendar.MONTH, mo);
        c.set(Calendar.DATE, d);
        c.set(Calendar.HOUR_OF_DAY, h);
        c.set(Calendar.MINUTE, mi);
        c.set(Calendar.SECOND, 0);
        c.set(Calendar.MILLISECOND, 0);
        return c.getTime();
    }

    static int daysIn(int y, int mo) {
        if (mo == 1) return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 ? 29 : 28;
        return mo == 3 || mo == 5 || mo == 8 || mo == 10 ? 30 : 31;
    }

    private int[] parts() {
        if (mode == DATE) return new int[] {DAY, MONTH, YEAR};
        if (mode == TIME) return new int[] {HOUR, MINUTE};
        return new int[] {DAY, MONTH, YEAR, HOUR, MINUTE};
    }

    private static String two(int v) { return v < 10 ? "0" + v : String.valueOf(v); }

    /** The parts' texts, in parts() order. */
    private String[] texts() {
        int[] p = parts();
        String[] s = new String[p.length];
        Calendar c = date != null ? cal(date.getTime()) : null;
        for (int i = 0; i < p.length; i++) {
            if (c == null) {
                s[i] = p[i] == YEAR ? "----" : "--";
                continue;
            }
            switch (p[i]) {
                case DAY: s[i] = two(c.get(Calendar.DATE)); break;
                case MONTH: s[i] = two(c.get(Calendar.MONTH) + 1); break;
                case YEAR: s[i] = String.valueOf(c.get(Calendar.YEAR)); break;
                case HOUR: s[i] = two(c.get(Calendar.HOUR_OF_DAY)); break;
                default: s[i] = two(c.get(Calendar.MINUTE)); break;
            }
        }
        return s;
    }

    /** What comes after part i: "/" inside the date, ":" inside the time, a space between. */
    private String sep(int i) {
        int[] p = parts();
        if (i + 1 >= p.length) return "";
        if (p[i] == DAY || p[i] == MONTH) return "/";
        if (p[i] == HOUR) return ":";
        return "   ";
    }

    private void change(int delta) {
        if (date == null) date = normalize(System.currentTimeMillis());
        Calendar c = cal(date.getTime());
        int y = c.get(Calendar.YEAR), mo = c.get(Calendar.MONTH), d = c.get(Calendar.DATE);
        int h = c.get(Calendar.HOUR_OF_DAY), mi = c.get(Calendar.MINUTE);
        switch (parts()[part]) {
            case DAY: d = wrap(d - 1 + delta, daysIn(y, mo)) + 1; break;
            case MONTH: mo = wrap(mo + delta, 12); break;
            case YEAR: y = Math.max(1, Math.min(9999, y + delta)); break;
            case HOUR: h = wrap(h + delta, 24); break;
            default: mi = wrap(mi + delta, 60); break;
        }
        if (d > daysIn(y, mo)) d = daysIn(y, mo);
        date = normalize(make(y, mo, d, h, mi).getTime());
        changed();
        stateChanged();
    }

    private static int wrap(int v, int n) { return ((v % n) + n) % n; }

    // ---- on the Form
    boolean focusable() { return true; }

    int contentHeight(int w) { return Font.getDefaultFont().getHeight() + 2 * Ui.PAD; }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        Ui.card(g, x, y, w, h, focused);
        Font f = Font.getDefaultFont();
        g.setFont(f);
        String[] t = texts();
        int tx = x + Ui.PAD, ty = y + Ui.PAD;
        for (int i = 0; i < t.length; i++) {
            int tw = f.stringWidth(t[i]);
            boolean on = focused && editing && i == part;
            if (on) {
                g.setColor(Screen.ACCENT);
                g.fillRoundRect(tx - 2, ty - 1, tw + 4, f.getHeight() + 2, 6, 6);
            }
            g.setColor(on ? Ui.WHITE : date == null ? Screen.MUTED : Screen.INK);
            g.drawString(t[i], tx, ty, Graphics.LEFT | Graphics.TOP);
            tx += tw + 2;
            String s = sep(i);
            g.setColor(Screen.MUTED);
            g.drawString(s, tx, ty, Graphics.LEFT | Graphics.TOP);
            tx += f.stringWidth(s) + 2;
        }
    }

    boolean keyItem(int type, int code) {
        if (type == 2) return false;
        int a = Canvas.action(code);
        if (a == Canvas.FIRE && type == 1) {
            if (!editing && date == null) {
                date = normalize(System.currentTimeMillis());
                stateChanged();
            }
            editing = !editing;
            changed();
            return true;
        }
        if (!editing) return super.keyItem(type, code);
        int n = parts().length;
        if (a == Canvas.LEFT || a == Canvas.RIGHT) {
            part = wrap(part + (a == Canvas.LEFT ? -1 : 1), n);
            changed();
            return true;
        }
        if (a == Canvas.UP || a == Canvas.DOWN) {
            change(a == Canvas.UP ? 1 : -1);
            return true;
        }
        return true;  // editing keeps the keys
    }

    void focusOut() {
        editing = false;
    }

    void pointerItem(int type, int x, int y, int w) {
        if (type != 5) return;
        Font f = Font.getDefaultFont();
        String[] t = texts();
        int tx = Ui.PAD, hit = -1;
        for (int i = 0; i < t.length; i++) {
            int tw = f.stringWidth(t[i]) + 2 + f.stringWidth(sep(i)) + 2;
            if (x >= tx - 4 && x < tx + tw) hit = i;
            tx += tw;
        }
        if (hit < 0) {
            keyItem(1, Canvas.K_FIRE);
            return;
        }
        if (date == null) {
            date = normalize(System.currentTimeMillis());
            stateChanged();
        }
        if (editing && hit == part) change(1);  // tapping the chosen part counts it up
        editing = true;
        part = hit;
        changed();
    }
}
