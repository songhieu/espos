package javax.microedition.lcdui;

import java.util.Vector;

/**
 * A card over the screen it interrupts (that screen dimmed): the type's mark or the image, the title, the text
 * (wrapped, scrolled with up / down when long), the indicator. Display runs the timeout; with no command of the app's,
 * DISMISS_COMMAND sits on the left soft key; OK (or a tap) runs the alert's only command.
 */
public class Alert extends Screen {
    public static final int FOREVER = -2;
    public static final Command DISMISS_COMMAND = new Command("Đóng", Command.OK, 0);

    private static final int MARK = 30;

    /** What comes after it (set by Display.setCurrent). */
    Displayable after;
    private String text;
    private Image image;
    private AlertType type;
    private int timeout;
    private Gauge indicator;
    private boolean fresh = true;  // dim the screen behind at the next paint (first paint after being shown)
    private int cardH, scroll, maxScroll;
    private Vector titleLines, textLines;
    private int linesW = -1;

    public Alert(String title) { this(title, null, null, null); }

    public Alert(String title, String alertText, Image alertImage, AlertType alertType) {
        this.title = title;
        text = alertText;
        image = ImageItem.snapshot(alertImage);
        type = alertType;
        timeout = getDefaultTimeout();
    }

    public int getDefaultTimeout() { return 2000; }

    /** FOREVER while it has two or more commands (it is then modal). */
    public int getTimeout() { return commands.size() > 1 ? FOREVER : timeout; }

    public void setTimeout(int time) {
        if (time <= 0 && time != FOREVER) throw new IllegalArgumentException();
        timeout = time;
    }

    public AlertType getType() { return type; }

    public void setType(AlertType type) {
        this.type = type;
        invalidate();
    }

    public String getString() { return text; }

    public void setString(String str) {
        text = str;
        textLines = null;
        invalidate();
    }

    public Image getImage() { return image; }

    public void setImage(Image img) {
        image = ImageItem.snapshot(img);
        invalidate();
    }

    public void setIndicator(Gauge indicator) {
        if (indicator != null) {
            if (indicator.owner != null && indicator.owner != this) throw new IllegalStateException();
            if (indicator.isInteractive() || indicator.label != null || !indicator.itemCommands.isEmpty()
                    || indicator.commandListener != null || indicator.layout != Item.LAYOUT_DEFAULT
                    || indicator.prefW != -1 || indicator.prefH != -1)
                throw new IllegalArgumentException();
        }
        if (this.indicator != null) this.indicator.owner = null;
        this.indicator = indicator;
        if (indicator != null) indicator.owner = this;
        invalidate();
    }

    public Gauge getIndicator() { return indicator; }

    public void addCommand(Command cmd) {
        if (cmd == null) throw new NullPointerException();
        if (cmd != DISMISS_COMMAND) super.addCommand(cmd);
    }

    public void removeCommand(Command cmd) {
        if (cmd != DISMISS_COMMAND) super.removeCommand(cmd);
    }

    public void setTitle(String s) {
        titleLines = null;
        super.setTitle(s);
    }

    // ---- what Display uses
    /** The app's commands (DISMISS_COMMAND is not counted). */
    int softCommandsCount() { return commands.size(); }

    Vector softCommands() {
        if (!commands.isEmpty()) return commands;
        Vector v = new Vector();
        v.addElement(DISMISS_COMMAND);
        return v;
    }

    /** The timeout ran out (or OK on the dismiss command): the app's listener hears DISMISS_COMMAND, else it goes. */
    void dismiss() {
        if (listener != null) listener.commandAction(DISMISS_COMMAND, this);
        else goAway();
    }

    private void goAway() {
        Display d = Display.instance;
        if (d != null && d.current == this && after != null) d.setCurrent(after);
    }

    /** With no listener of the app's, every command dismisses it (MIDP's default listener). */
    void fireCommand(Command c) {
        if (listener != null) listener.commandAction(c, this);
        else goAway();
    }

    void showing() {
        if (fresh) {
            cardH = 0;
            scroll = 0;
            if (type != null && Display.instance != null) type.playSound(Display.instance);
        }
    }

    void hiding() {
        if (!shown) fresh = true;  // really gone (a pause keeps it on the screen)
    }

    // ---- drawing
    private int innerW() { return Display.W - 2 * Ui.M - 2 * 14; }

    private void wrapAll(int iw) {
        if (linesW != iw) {
            titleLines = textLines = null;
            linesW = iw;
        }
        if (titleLines == null) titleLines = Screen.wrap(title, Ui.bold(), iw);
        if (textLines == null) textLines = Screen.wrap(text, Font.getDefaultFont(), iw);
    }

    /** The scrolled part's height: title, text, indicator. */
    private int bodyHeight(int iw) {
        int h = Ui.height(titleLines, Ui.bold());
        if (textLines.size() > 0) h += (h > 0 ? 6 : 0) + Ui.height(textLines, Font.getDefaultFont());
        if (indicator != null) h += 10 + indicator.height(iw);
        return h;
    }

    private int headHeight() {
        if (image != null) return image.getHeight() + 10;
        return type != null ? MARK + 10 : 0;
    }

    void paintScreen(Graphics g, int w, int h) {
        if (fresh) {
            int[] px = g.buf;
            for (int i = 0; i < px.length; i++) px[i] = 0xFF000000 | ((px[i] >> 1) & 0x7F7F7F);
            fresh = false;
        }
        int iw = innerW(), cw = w - 2 * Ui.M;
        wrapAll(iw);
        int head = headHeight(), body = bodyHeight(iw);
        int need = 14 + head + body + 14;
        int room = h - 24;
        if (need > cardH) cardH = Math.min(need, room);  // only grows while shown: the dimmed screen stays covered
        int bodyRoom = cardH - 28 - head;
        maxScroll = Math.max(0, body - bodyRoom);
        if (scroll > maxScroll) scroll = maxScroll;
        int cy = (h - cardH) / 2;
        g.setClip(0, 0, w, h);
        g.setColor(Screen.CARD);
        g.fillRoundRect(Ui.M, cy, cw, cardH, 16, 16);
        int y = cy + 14, cx = w / 2;
        if (image != null) {
            g.clipRect(Ui.M + 14, y, iw, image.getHeight());
            g.drawImage(image, cx, y, Graphics.HCENTER | Graphics.TOP);
            g.setClip(0, 0, w, h);
        } else if (type != null) {
            paintMark(g, cx, y + MARK / 2);
        }
        y += head;
        g.clipRect(Ui.M, y, cw, bodyRoom);
        y -= scroll;
        Font bf = Ui.bold(), f = Font.getDefaultFont();
        g.setFont(bf);
        g.setColor(Screen.INK);
        for (int i = 0; i < titleLines.size(); i++, y += bf.getHeight())
            g.drawString((String) titleLines.elementAt(i), cx, y, Graphics.HCENTER | Graphics.TOP);
        if (textLines.size() > 0) {
            if (titleLines.size() > 0) y += 6;
            g.setFont(f);
            g.setColor(Screen.MUTED);
            for (int i = 0; i < textLines.size(); i++, y += f.getHeight())
                g.drawString((String) textLines.elementAt(i), cx, y, Graphics.HCENTER | Graphics.TOP);
        }
        if (indicator != null) indicator.paint(g, Ui.M + 14, y + 10, iw, false);
        g.setClip(0, 0, w, h);
        if (maxScroll > 0) {
            int vy = cy + 14 + head, th = Math.max(12, bodyRoom * bodyRoom / body);
            g.setColor(0xC4C9D2);
            g.fillRoundRect(w - Ui.M - 6, vy + (bodyRoom - th) * scroll / maxScroll, 3, th, 3, 3);
        }
    }

    private void paintMark(Graphics g, int cx, int cy) {
        int r = MARK / 2;
        g.setColor(type.color());
        g.fillArc(cx - r, cy - r, MARK, MARK, 0, 360);
        g.setColor(Ui.WHITE);
        if (type == AlertType.ERROR) {
            for (int d = -1; d <= 1; d++) {
                g.drawLine(cx - 6 + d, cy - 6, cx + 6 + d, cy + 6);
                g.drawLine(cx + 6 + d, cy - 6, cx - 6 + d, cy + 6);
            }
        } else if (type == AlertType.CONFIRMATION) {
            for (int d = -1; d <= 1; d++) {
                g.drawLine(cx - 7, cy + d, cx - 2, cy + 5 + d);
                g.drawLine(cx - 2, cy + 5 + d, cx + 7, cy - 5 + d);
            }
        } else {
            boolean info = type == AlertType.INFO;
            g.fillRect(cx - 1, info ? cy - 2 : cy - 8, 3, info ? 10 : 10);
            g.fillRect(cx - 1, info ? cy - 8 : cy + 5, 3, 3);
        }
    }

    boolean keyScreen(int type, int code) {
        if (type == 2) return false;
        int a = Canvas.action(code);
        if (a == Canvas.UP || a == Canvas.DOWN) {
            int s = Math.max(0, Math.min(maxScroll, scroll + (a == Canvas.UP ? -1 : 1) * 2 * Font.getDefaultFont().getHeight()));
            if (s == scroll) return false;
            scroll = s;
            return true;
        }
        if (a == Canvas.FIRE && type == 1) {
            Vector c = softCommands();
            if (c.size() == 1) {
                Command only = (Command) c.elementAt(0);
                if (only == DISMISS_COMMAND) dismiss();
                else fireCommand(only);
                return true;
            }
        }
        return false;
    }

    private int pressY, pressScroll;
    private boolean dragged;

    void pointerScreen(int type, int x, int y) {
        if (type == 4) {
            pressY = y;
            pressScroll = scroll;
            dragged = false;
        } else if (type == 6) {
            if (Math.abs(y - pressY) > 8) dragged = true;
            if (dragged && maxScroll > 0) {
                scroll = Math.max(0, Math.min(maxScroll, pressScroll - (y - pressY)));
                invalidate();
            }
        } else if (!dragged) {
            keyScreen(1, Canvas.K_FIRE);
        }
    }
}
