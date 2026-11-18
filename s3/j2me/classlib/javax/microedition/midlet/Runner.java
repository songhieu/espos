package javax.microedition.midlet;

import com.espos.j2me.Lifecycle;
import javax.microedition.lcdui.DisplayHost;

/** The game's start: Runner.main({"the.MIDlet.Class"}) (src/j2me/midp.cpp). Runs the display loop until the end. */
public final class Runner {
    static boolean ended;

    private Runner() {}

    public static void main(String[] args) throws Exception {
        final MIDlet m = (MIDlet) Class.forName(args[0]).newInstance();
        DisplayHost.start(new Lifecycle() {
            public void pause() { m.pauseApp(); }
            public void resume() {
                try {
                    m.startApp();
                } catch (MIDletStateChangeException e) {
                }
            }
            public void destroy() {
                try {
                    m.destroyApp(true);
                } catch (MIDletStateChangeException e) {
                }
            }
        });
        try {
            m.startApp();
        } catch (MIDletStateChangeException e) {
            com.espos.j2me.Native.exit();
            return;
        }
        if (!ended) DisplayHost.loop();
    }
}
