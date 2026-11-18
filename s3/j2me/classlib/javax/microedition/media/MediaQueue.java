package javax.microedition.media;

import com.espos.j2me.Native;
import java.util.Vector;

/**
 * One thread for every player: it delivers PlayerListener events in the order they were posted (never holding a
 * player's lock) and runs the players' timers (the end of the media).
 */
final class MediaQueue implements Runnable {
    private static final Object lock = new Object();
    private static final Vector jobs = new Vector();    // Job, posted
    private static final Vector timers = new Vector();  // Job with at > 0
    private static Thread thread;

    private static final class Job {
        final BasePlayer player;
        final String event;          // an event, or null for a timer
        final Object data;
        final PlayerListener[] to;
        final long at;
        final int token;

        Job(BasePlayer p, String ev, Object d, PlayerListener[] to, long at, int token) {
            player = p;
            event = ev;
            data = d;
            this.to = to;
            this.at = at;
            this.token = token;
        }
    }

    private MediaQueue() {}

    /** Delivers ev to the listeners (taken now) soon, on the media thread. */
    static void post(BasePlayer p, String ev, Object data, PlayerListener[] to) {
        if (to == null || to.length == 0) return;
        synchronized (lock) {
            jobs.addElement(new Job(p, ev, data, to, 0, 0));
            wake();
        }
    }

    /** Calls p.timer(token) at the time at (ms), on the media thread. */
    static void at(BasePlayer p, long at, int token) {
        synchronized (lock) {
            timers.addElement(new Job(p, null, null, null, at, token));
            wake();
        }
    }

    private static void wake() {
        if (thread == null) {
            thread = new Thread(new MediaQueue(), "media");
            thread.start();
        }
        lock.notify();
    }

    public void run() {
        for (;;) {
            Job j = null;
            synchronized (lock) {
                while (j == null) {
                    if (!jobs.isEmpty()) {
                        j = (Job) jobs.elementAt(0);
                        jobs.removeElementAt(0);
                        break;
                    }
                    long now = System.currentTimeMillis(), next = Long.MAX_VALUE;
                    for (int i = 0; i < timers.size(); i++) {
                        Job t = (Job) timers.elementAt(i);
                        if (t.player.token != t.token) {  // read without the player's lock (lock order)
                            timers.removeElementAt(i--);
                        } else if (t.at <= now) {
                            j = t;
                            timers.removeElementAt(i);
                            break;
                        } else if (t.at < next) {
                            next = t.at;
                        }
                    }
                    if (j != null) break;
                    try {
                        lock.wait(next == Long.MAX_VALUE ? 0 : Math.max(1, next - now));
                    } catch (InterruptedException e) {
                    }
                }
            }
            try {
                if (j.event == null) {
                    j.player.timer(j.token);
                } else {
                    for (int i = 0; i < j.to.length; i++) j.to[i].playerUpdate(j.player, j.event, j.data);
                }
            } catch (Throwable e) {
                e.printStackTrace();
            }
        }
    }

    /** Native.tone, harmless where there is no sound (or no natives, as in tests on a PC). */
    static void tone(int note, int ms, int volume) {
        try {
            Native.tone(note, ms, volume);
        } catch (Throwable e) {
        }
    }
}
