package javax.microedition.lcdui;

import com.espos.j2me.Native;
import java.util.Vector;
import javax.microedition.midlet.MIDlet;

/**
 * The one display. Its loop (DisplayHost.loop, on the game's main thread) takes the device's events, runs
 * callSerially work, and paints the current screen into the screen's pixels when asked, then shows them
 * (Native.flush). Painting happens under paintLock, from the loop or from serviceRepaints in the caller's thread.
 */
public class Display {
    public static final int LIST_ELEMENT = 1, CHOICE_GROUP_ELEMENT = 2, ALERT = 3;
    public static final int COLOR_BACKGROUND = 0, COLOR_FOREGROUND = 1, COLOR_HIGHLIGHTED_BACKGROUND = 2,
            COLOR_HIGHLIGHTED_FOREGROUND = 3, COLOR_BORDER = 4, COLOR_HIGHLIGHTED_BORDER = 5;

    static final int W = Native.screenWidth(), H = Native.screenHeight();
    static Display instance;

    final Object lock = new Object();       // the loop waits on it; repaint and callSerially wake it
    final Object paintLock = new Object();  // one paint at a time
    final int[] screen = new int[W * H];
    final Graphics sg = new Graphics(screen, W, H);
    Displayable current;
    private final Vector serial = new Vector();
    private boolean dirty;
    private int dx1, dy1, dx2, dy2;  // the area to paint (union of repaint requests)
    // the soft keys
    private Command left, right;
    private Vector leftMenu;  // more than one command for the left key: it opens this menu
    private Vector menu;      // the command menu, open
    private int menuSel;
    // an alert shown: when it goes, and what comes after it
    private long alertUntil;

    Display() {}

    public static Display getDisplay(MIDlet m) {
        if (m == null) throw new NullPointerException();
        if (instance == null) instance = new Display();
        return instance;
    }

    public Displayable getCurrent() { return current; }

    public void setCurrent(Displayable next) {
        if (next instanceof Alert) {
            // an alert over an alert: what comes after is what the first one led to
            setCurrent((Alert) next, current instanceof Alert ? ((Alert) current).after : current);
            return;
        }
        show(next);
    }

    public void setCurrent(Alert alert, Displayable nextDisplayable) {
        if (alert == null) throw new NullPointerException();
        if (nextDisplayable instanceof Alert) throw new IllegalArgumentException();
        alert.after = nextDisplayable != null ? nextDisplayable : current;
        int t = alert.getTimeout();
        alertUntil = t == Alert.FOREVER || alert.softCommandsCount() > 1 ? 0 : System.currentTimeMillis() + t;
        show(alert);
    }

    public void setCurrentItem(Item item) {
        Screen owner = item.owner;
        if (owner != null) {
            show(owner);
            owner.focusItem(item);
        }
    }

    private void show(Displayable next) {
        if (next == null) return;  // MIDP: null means "bring the app to the foreground"
        synchronized (paintLock) {
            Displayable old = current;
            if (old == next) {
                requestRepaint(next, 0, 0, W, H);
                return;
            }
            menu = null;
            current = next;
            if (!(next instanceof Alert)) alertUntil = 0;
            if (old != null) {
                old.shown = false;
                try {
                    old.hiding();
                } catch (Throwable e) {
                    e.printStackTrace();
                }
            }
            next.shown = true;
            try {
                next.showing();
            } catch (Throwable e) {
                e.printStackTrace();
            }
            if (next instanceof Canvas) {
                // a port for another phone may hard-code its screen in getWidth/getHeight (one game's Canvas subclass
                // says 320 x 240 whatever the device is): the device can reopen the game at that size
                int w = next.getWidth(), h = next.getHeight();
                if ((w != W || h != H) && w >= 96 && h >= 96 && w <= 400 && h <= 400) Native.screenHint(w, h);
            }
            commandsChanged();
            requestRepaint(next, 0, 0, W, H);
        }
    }

    public void callSerially(Runnable r) {
        synchronized (lock) {
            serial.addElement(r);
            lock.notify();
        }
    }

    public boolean flashBacklight(int duration) { return false; }

    public boolean vibrate(int duration) {
        if (duration < 0) throw new IllegalArgumentException();
        Native.vibrate(duration);
        return true;
    }

    public boolean isColor() { return true; }
    public int numColors() { return 65536; }
    public int numAlphaLevels() { return 256; }

    public int getColor(int colorSpecifier) {
        switch (colorSpecifier) {
            case COLOR_BACKGROUND: return Screen.BG;
            case COLOR_FOREGROUND: return Screen.INK;
            case COLOR_HIGHLIGHTED_BACKGROUND: return Screen.ACCENT;
            case COLOR_HIGHLIGHTED_FOREGROUND: return 0xFFFFFF;
            case COLOR_BORDER: return Screen.LINE;
            case COLOR_HIGHLIGHTED_BORDER: return Screen.ACCENT;
        }
        throw new IllegalArgumentException();
    }

    public int getBorderStyle(boolean highlighted) { return Graphics.SOLID; }

    public int getBestImageWidth(int imageType) {
        if (imageType < 1 || imageType > 3) throw new IllegalArgumentException();
        return imageType == ALERT ? W - 40 : 24;
    }

    public int getBestImageHeight(int imageType) {
        if (imageType < 1 || imageType > 3) throw new IllegalArgumentException();
        return imageType == ALERT ? 80 : 24;
    }

    // ---- painting

    void requestRepaint(Displayable d, int x, int y, int w, int h) {
        if (d != current) return;
        synchronized (lock) {
            int x2 = Math.min(W, x + w), y2 = Math.min(H, y + h);
            x = Math.max(0, x);
            y = Math.max(0, y);
            if (x >= x2 || y >= y2) return;
            if (!dirty) {
                dx1 = x;
                dy1 = y;
                dx2 = x2;
                dy2 = y2;
                dirty = true;
            } else {
                dx1 = Math.min(dx1, x);
                dy1 = Math.min(dy1, y);
                dx2 = Math.max(dx2, x2);
                dy2 = Math.max(dy2, y2);
            }
            lock.notify();
        }
    }

    void serviceRepaints(Displayable d) {
        if (d == current) paintPending();
    }

    /** Paints what was asked, if anything. */
    void paintPending() {
        synchronized (paintLock) {
            int x1, y1, x2, y2;
            synchronized (lock) {
                if (!dirty) return;
                dirty = false;
                x1 = dx1;
                y1 = dy1;
                x2 = dx2;
                y2 = dy2;
            }
            Displayable d = current;
            if (d == null) return;
            sg.reset();
            if (menu != null || d instanceof Screen) {
                x1 = y1 = 0;
                x2 = W;
                y2 = H;
            } else {
                sg.setClip(x1, y1, x2 - x1, y2 - y1);
            }
            try {
                d.paintFrame(sg);
            } catch (Throwable e) {
                e.printStackTrace();
            }
            if (menu != null) paintMenu();
            Native.flush(screen, x1, y1, x2 - x1, y2 - y1);
        }
    }

    /** GameCanvas.flushGraphics: its buffer's region to the screen at once. */
    void flushBuffer(Displayable d, int[] px, int x, int y, int w, int h) {
        if (d != current) return;
        synchronized (paintLock) {
            if (menu != null) return;  // the command menu is over it: the game waits
            x = Math.max(0, x);
            y = Math.max(0, y);
            w = Math.min(W - x, w);
            h = Math.min(H - y, h);
            if (w <= 0 || h <= 0) return;
            for (int r = 0; r < h; r++) System.arraycopy(px, (y + r) * W + x, screen, (y + r) * W + x, w);
            Native.flush(screen, x, y, w, h);
        }
    }

    // ---- soft keys and the command menu

    void commandsChanged() {
        Displayable d = current;
        left = right = null;
        leftMenu = null;
        if (d == null) {
            Native.softLabels("", "");
            return;
        }
        Vector all = d.softCommands();
        Vector l = new Vector();
        for (int i = 0; i < all.size(); i++) {
            Command c = (Command) all.elementAt(i);
            if (right == null && c.negative()) right = c;
            else l.addElement(c);
        }
        if (right == null && l.size() == 2) {  // two commands, none "back": one on each key
            right = (Command) l.elementAt(1);
            l.removeElementAt(1);
        }
        if (l.size() == 1) left = (Command) l.elementAt(0);
        else if (l.size() > 1) leftMenu = l;
        Native.softLabels(leftMenu != null ? "Chọn" : left != null ? left.getLabel() : "", right != null ? right.getLabel() : "");
    }

    private void paintMenu() {
        Font f = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_MEDIUM);
        int rowH = f.getHeight() + 14, n = menu.size(), h = n * rowH + 12;
        int y0 = H - h;
        sg.reset();
        // the screen behind, dimmed
        for (int i = 0; i < screen.length; i++) {
            int c = screen[i];
            screen[i] = 0xFF000000 | ((c >> 1) & 0x7F7F7F);
        }
        sg.setColor(Screen.CARD);
        sg.fillRoundRect(6, y0, W - 12, h - 6, 16, 16);
        sg.setFont(f);
        for (int i = 0; i < n; i++) {
            int y = y0 + 6 + i * rowH;
            if (i == menuSel) {
                sg.setColor(Screen.ACCENT);
                sg.fillRoundRect(12, y, W - 24, rowH - 2, 10, 10);
            }
            sg.setColor(i == menuSel ? 0xFFFFFF : Screen.INK);
            Command c = (Command) menu.elementAt(i);
            String s = c.getLongLabel() != null ? c.getLongLabel() : c.getLabel();
            sg.drawString(s, 22, y + (rowH - f.getHeight()) / 2, Graphics.LEFT | Graphics.TOP);
        }
    }

    private void fire(Command c) {
        Displayable d = current;
        try {
            if (d != null) d.fireCommand(c);
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    /** Soft keys and the menu take keys first; true when they did. */
    private boolean softKey(int type, int code) {
        if (menu != null) {
            if (type == 2) return true;
            int a = Canvas.action(code);
            if (a == Canvas.UP) menuSel = (menuSel + menu.size() - 1) % menu.size();
            else if (a == Canvas.DOWN) menuSel = (menuSel + 1) % menu.size();
            else if (a == Canvas.FIRE || code == Canvas.K_SOFT_LEFT) {
                Command c = (Command) menu.elementAt(menuSel);
                menu = null;
                fire(c);
            } else if (code == Canvas.K_SOFT_RIGHT) {
                menu = null;
            } else {
                return true;
            }
            requestRepaint(current, 0, 0, W, H);
            return true;
        }
        if (code != Canvas.K_SOFT_LEFT && code != Canvas.K_SOFT_RIGHT) return false;
        Command c = code == Canvas.K_SOFT_LEFT ? left : right;
        if (c == null && !(code == Canvas.K_SOFT_LEFT && leftMenu != null)) return false;  // the game gets -6 / -7
        if (type != 1) return true;
        if (c != null) {
            fire(c);
        } else {
            menu = leftMenu;
            menuSel = 0;
            requestRepaint(current, 0, 0, W, H);
        }
        return true;
    }

    // ---- the loop

    private boolean paused;

    void loop(com.espos.j2me.Lifecycle life) {
        int[] ev = new int[4];
        for (;;) {
            boolean busy = false;
            int t;
            while ((t = Native.poll(ev)) != 0) {
                busy = true;
                Displayable d = current;
                try {
                    if (t <= 3) {
                        if (!softKey(t, ev[0]) && d != null && !paused) d.key(t, ev[0]);
                    } else if (t <= 6) {
                        if (menu != null) {
                            if (t == 4) {
                                menu = null;
                                requestRepaint(current, 0, 0, W, H);
                            }
                        } else if (d != null && !paused) {
                            d.pointer(t, ev[0], ev[1]);
                        }
                    } else if (t == 7 && !paused) {
                        paused = true;
                        if (d != null) d.hiding();
                        life.pause();
                    } else if (t == 8 && paused) {
                        paused = false;
                        life.resume();
                        if (current != null) {
                            current.showing();
                            requestRepaint(current, 0, 0, W, H);
                        }
                    } else if (t == 9) {
                        life.destroy();
                        Native.exit();
                        return;
                    }
                } catch (Throwable e) {
                    e.printStackTrace();
                }
            }
            for (;;) {
                Runnable r;
                synchronized (lock) {
                    if (serial.isEmpty()) break;
                    r = (Runnable) serial.elementAt(0);
                    serial.removeElementAt(0);
                }
                busy = true;
                try {
                    r.run();
                } catch (Throwable e) {
                    e.printStackTrace();
                }
            }
            if (alertUntil != 0 && System.currentTimeMillis() >= alertUntil && current instanceof Alert) {
                alertUntil = 0;
                ((Alert) current).dismiss();
            }
            if (!paused) paintPending();
            if (!busy) {
                synchronized (lock) {
                    if (!dirty && serial.isEmpty()) {
                        try {
                            lock.wait(15);
                        } catch (InterruptedException e) {
                        }
                    }
                }
            }
        }
    }
}
