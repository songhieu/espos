package javax.microedition.midlet;

import com.espos.j2me.Native;

public abstract class MIDlet {
    protected MIDlet() {}

    protected abstract void startApp() throws MIDletStateChangeException;
    protected abstract void pauseApp();
    protected abstract void destroyApp(boolean unconditional) throws MIDletStateChangeException;

    public final void notifyDestroyed() {
        Runner.ended = true;
        Native.exit();
    }

    public final void notifyPaused() {}
    public final void resumeRequest() {}
    public final String getAppProperty(String key) {
        if (key == null) throw new NullPointerException();
        return Native.appProperty(key);
    }
    public final boolean platformRequest(String URL) throws javax.microedition.io.ConnectionNotFoundException {
        if (URL == null || URL.length() == 0) return false;
        throw new javax.microedition.io.ConnectionNotFoundException("not supported");
    }
    public final int checkPermission(String permission) { return 0; }
}
