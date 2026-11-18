package javax.microedition.lcdui;

import java.util.Vector;

/**
 * One thread for the screens' timed repaints: the end of a multi-tap pause in text input, running gauges. It sleeps
 * until the next one is due (no polling) and only asks for a repaint; painting stays on the display loop. Not MIDP.
 */
final class Ticks implements Runnable {
    private static final Object lock = new Object();
    private static final Vector due = new Vector();  // of Ticks entries
    private static Thread thread;

    private final Screen screen;
    private long at;

    private Ticks(Screen s, long at) {
        screen = s;
        this.at = at;
    }

    /** Repaints s in ms (unless an earlier repaint of s is already due). */
    static void later(Screen s, int ms) {
        long at = System.currentTimeMillis() + ms;
        synchronized (lock) {
            for (int i = 0; i < due.size(); i++) {
                Ticks t = (Ticks) due.elementAt(i);
                if (t.screen == s) {
                    if (at < t.at) {
                        t.at = at;
                        lock.notify();
                    }
                    return;
                }
            }
            due.addElement(new Ticks(s, at));
            if (thread == null) {
                thread = new Thread(new Ticks(null, 0));
                thread.start();
            }
            lock.notify();
        }
    }

    public void run() {
        for (;;) {
            Screen fire = null;
            synchronized (lock) {
                long now = System.currentTimeMillis(), next = Long.MAX_VALUE;
                for (int i = 0; i < due.size() && fire == null; i++) {
                    Ticks t = (Ticks) due.elementAt(i);
                    if (t.at <= now) {
                        fire = t.screen;
                        due.removeElementAt(i);
                    } else if (t.at < next) {
                        next = t.at;
                    }
                }
                if (fire == null) {
                    try {
                        lock.wait(due.isEmpty() ? 0 : Math.max(1, next - now));
                    } catch (InterruptedException e) {
                    }
                    continue;
                }
            }
            if (fire.isShown()) fire.invalidate();
        }
    }
}
