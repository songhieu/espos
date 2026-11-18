package javax.microedition.lcdui;

import com.espos.j2me.Lifecycle;

/** For javax.microedition.midlet.Runner only (not a MIDP class): starts and runs the display loop. */
public final class DisplayHost {
    private static Lifecycle life;

    private DisplayHost() {}

    public static void start(Lifecycle l) {
        life = l;
        if (Display.instance == null) Display.instance = new Display();
    }

    /** Runs until the user leaves the game (the device asks) or the game ends (MIDlet.notifyDestroyed). */
    public static void loop() { Display.instance.loop(life); }
}
