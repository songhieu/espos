package javax.microedition.lcdui;

/**
 * A bar. Interactive: on a card with a knob and "value / max"; left / right (or a drag) change it. Not interactive: a
 * thin bar; INDEFINITE ones show a sliding piece (CONTINUOUS_RUNNING moves by itself, INCREMENTAL_UPDATING a step per
 * setValue) or an empty track when idle.
 */
public class Gauge extends Item {
    public static final int INDEFINITE = -1;
    public static final int CONTINUOUS_IDLE = 0, INCREMENTAL_IDLE = 1, CONTINUOUS_RUNNING = 2, INCREMENTAL_UPDATING = 3;

    private static final int KNOB = 16;

    private final boolean interactive;
    private int max, value;
    private int phase;  // INCREMENTAL_UPDATING steps

    public Gauge(String label, boolean interactive, int maxValue, int initialValue) {
        super(label);
        this.interactive = interactive;
        checkMax(maxValue);
        max = maxValue;
        if (max == INDEFINITE) {
            checkState(initialValue);
            value = initialValue;
        } else {
            value = clamp(initialValue);
        }
    }

    private void checkMax(int m) {
        if (m == INDEFINITE ? interactive : m <= 0) throw new IllegalArgumentException();
    }

    private static void checkState(int v) {
        if (v < CONTINUOUS_IDLE || v > INCREMENTAL_UPDATING) throw new IllegalArgumentException();
    }

    private int clamp(int v) { return v < 0 ? 0 : v > max ? max : v; }

    public void setValue(int v) {
        if (max == INDEFINITE) {
            checkState(v);
            if (v == INCREMENTAL_UPDATING) phase++;
            value = v;
        } else {
            value = clamp(v);
        }
        changed();
    }

    public int getValue() { return value; }

    public void setMaxValue(int maxValue) {
        checkMax(maxValue);
        if (maxValue == INDEFINITE) {
            if (max != INDEFINITE) value = CONTINUOUS_IDLE;
        } else if (max == INDEFINITE) {
            value = 0;
        }
        max = maxValue;
        if (max != INDEFINITE) value = clamp(value);
        changed();
    }

    public int getMaxValue() { return max; }

    public boolean isInteractive() { return interactive; }

    // ---- inside the library
    boolean focusable() { return interactive || super.focusable(); }

    boolean takesDrag() { return interactive; }

    private String valueText() { return value + " / " + max; }

    int contentHeight(int w) {
        return interactive ? KNOB + 2 * Ui.PAD : 14;
    }

    /** The track's left end and width for an interactive gauge at x, w. */
    private int trackX(int x) { return x + Ui.PAD + KNOB / 2; }

    private int trackW(int w) {
        return w - 2 * Ui.PAD - KNOB - 8 - Ui.small().stringWidth(max + " / " + max);
    }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        if (interactive) {
            Ui.card(g, x, y, w, h, focused);
            int tx = trackX(x), tw = trackW(w), cy = y + h / 2;
            g.setColor(Screen.LINE);
            g.fillRoundRect(tx, cy - 3, tw, 6, 6, 6);
            int kx = tx + (max > 0 ? tw * value / max : 0);
            g.setColor(Screen.ACCENT);
            if (kx > tx) g.fillRoundRect(tx, cy - 3, kx - tx, 6, 6, 6);
            g.fillArc(kx - KNOB / 2, cy - KNOB / 2, KNOB, KNOB, 0, 360);
            g.setColor(Ui.WHITE);
            g.fillArc(kx - 3, cy - 3, 6, 6, 0, 360);
            Font f = Ui.small();
            g.setFont(f);
            g.setColor(Screen.MUTED);
            g.drawString(valueText(), x + w - Ui.PAD, cy - f.getHeight() / 2, Graphics.RIGHT | Graphics.TOP);
            return;
        }
        int ty = y + 3, th = 8;
        g.setColor(Screen.LINE);
        g.fillRoundRect(x, ty, w, th, th, th);
        g.setColor(Screen.ACCENT);
        if (max != INDEFINITE) {
            int fw = w * value / max;
            if (fw > 0) g.fillRoundRect(x, ty, Math.max(fw, th), th, th, th);
            return;
        }
        if (value == CONTINUOUS_IDLE || value == INCREMENTAL_IDLE) return;
        int pw = w / 4, span = w + pw;
        int pos = value == CONTINUOUS_RUNNING ? (int) ((System.currentTimeMillis() / 12) % span) : (phase * 12) % span;
        int cx = g.getClipX(), cy = g.getClipY(), cw = g.getClipWidth(), ch = g.getClipHeight();
        g.clipRect(x, ty, w, th);
        g.fillRoundRect(x + pos - pw, ty, pw, th, th, th);
        g.setClip(cx, cy, cw, ch);
        if (value == CONTINUOUS_RUNNING && owner != null) Ticks.later(owner, 60);  // keeps moving while shown
    }

    boolean keyItem(int type, int code) {
        if (!interactive || type == 2) return super.keyItem(type, code);
        int a = Canvas.action(code);
        if (a == Canvas.LEFT || a == Canvas.RIGHT) {
            int step = max <= 20 ? 1 : (max + 19) / 20;
            int v = clamp(value + (a == Canvas.LEFT ? -step : step));
            if (v != value) {
                value = v;
                changed();
                stateChanged();
            }
            return true;
        }
        return super.keyItem(type, code);
    }

    void pointerItem(int type, int x, int y, int w) {
        if (!interactive) {
            super.pointerItem(type, x, y, w);
            return;
        }
        int tx = trackX(0), tw = trackW(w);
        int v = clamp(((x - tx) * max + tw / 2) / Math.max(1, tw));
        if (v != value) {
            value = v;
            changed();
            stateChanged();
        }
    }
}
