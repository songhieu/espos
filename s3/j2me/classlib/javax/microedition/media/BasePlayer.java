package javax.microedition.media;

import java.util.Vector;
import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;

/**
 * Every Player. Sampled audio (wav, midi, mp3, amr) makes no sound on EspOS yet: it keeps time as if it played
 * (a wav's real length, else a short guess) so games see STARTED, END_OF_MEDIA, loops and states as on a phone.
 * Tone sequences (device://tone, audio/x-tone-seq) play through Native.tone, note by note on a thread.
 * Lenient where phones differ: controls and the content type are given in the UNREALIZED state too.
 */
final class BasePlayer implements Player {
    private final String type;
    private final boolean tones;
    private int state = UNREALIZED;
    private long durationUs;       // TIME_UNKNOWN when not known
    private final long guessUs;    // how long a play lasts when the duration is not known
    private int loopCount = 1, loopsLeft = 1;
    private long mediaUs;          // the media time when not started; where the play began when started
    private long startedAt;        // ms, when STARTED
    private boolean atEnd;
    /** Changes whenever the play is cut (stop, seek, loop, close): timers and the tone thread of older plays stop. */
    volatile int token;
    private final Vector listeners = new Vector();
    private final Volume volume = new Volume();
    private final Tone toneControl;
    private ToneSequence seq;
    private TimeBase timeBase = Manager.getSystemTimeBase();

    BasePlayer(String type, long durationUs, long guessUs, ToneSequence seq) {
        this.type = type;
        this.tones = "audio/x-tone-seq".equals(type);
        this.durationUs = durationUs;
        this.guessUs = guessUs;
        this.seq = seq;
        toneControl = tones ? new Tone() : null;
        if (seq != null) this.durationUs = seq.durationUs;
        else if (tones) this.durationUs = 0;
    }

    // ---- states

    public synchronized void realize() throws MediaException {
        open();
        if (state == UNREALIZED) state = REALIZED;
    }

    public synchronized void prefetch() throws MediaException {
        open();
        if (state < PREFETCHED) state = PREFETCHED;
    }

    public synchronized void start() throws MediaException {
        open();
        if (state == STARTED) return;
        if (state < PREFETCHED) state = PREFETCHED;
        if (atEnd) {
            atEnd = false;
            mediaUs = 0;
            loopsLeft = loopCount;
        }
        state = STARTED;
        startedAt = System.currentTimeMillis();
        play();
        post(PlayerListener.STARTED, new Long(mediaUs));
    }

    public synchronized void stop() throws MediaException {
        open();
        if (state != STARTED) return;
        mediaUs = mediaNow();
        state = PREFETCHED;
        cut();
        post(PlayerListener.STOPPED, new Long(mediaUs));
    }

    public synchronized void deallocate() {
        open();
        if (state == STARTED) {
            try {
                stop();
            } catch (MediaException e) {
            }
        }
        if (state == PREFETCHED) state = REALIZED;
    }

    public void close() {
        PlayerListener[] to;
        synchronized (this) {
            if (state == CLOSED) return;
            if (state == STARTED) mediaUs = mediaNow();
            cut();
            state = CLOSED;
            to = listeners();
            listeners.removeAllElements();
        }
        MediaQueue.post(this, PlayerListener.CLOSED, null, to);
    }

    public synchronized int getState() { return state; }

    // ---- time

    public synchronized void setTimeBase(TimeBase master) throws MediaException {
        open();
        if (state == STARTED) throw new IllegalStateException("started");
        timeBase = master != null ? master : Manager.getSystemTimeBase();
    }

    public synchronized TimeBase getTimeBase() {
        open();
        return timeBase;
    }

    public synchronized long setMediaTime(long now) throws MediaException {
        open();
        if (state == UNREALIZED) throw new IllegalStateException("unrealized");
        long len = length();
        if (now < 0) now = 0;
        if (now > len) now = len;
        mediaUs = now;
        atEnd = false;
        if (state == STARTED) {
            startedAt = System.currentTimeMillis();
            cut();
            play();
        }
        return now;
    }

    public synchronized long getMediaTime() {
        open();
        return state == STARTED ? mediaNow() : mediaUs;
    }

    public synchronized long getDuration() {
        open();
        return durationUs;
    }

    public synchronized String getContentType() {
        open();
        return type;
    }

    public synchronized void setLoopCount(int count) {
        open();
        if (state == STARTED) throw new IllegalStateException("started");
        if (count == 0 || count < -1) throw new IllegalArgumentException("loop count " + count);
        loopCount = loopsLeft = count;
    }

    // ---- listeners and controls

    public synchronized void addPlayerListener(PlayerListener l) {
        open();
        if (l != null && !listeners.contains(l)) listeners.addElement(l);
    }

    public synchronized void removePlayerListener(PlayerListener l) {
        open();
        listeners.removeElement(l);
    }

    public synchronized Control[] getControls() {
        open();
        return tones ? new Control[] {volume, toneControl} : new Control[] {volume};
    }

    public synchronized Control getControl(String controlType) {
        if (controlType == null) throw new IllegalArgumentException();
        open();
        String t = controlType;
        if (t.startsWith("javax.microedition.media.control.")) t = t.substring(33);
        if (t.equals("VolumeControl")) return volume;
        if (t.equals("ToneControl")) return toneControl;
        return null;
    }

    // ---- inside

    private void open() {
        if (state == CLOSED) throw new IllegalStateException("closed");
    }

    /** How long one play lasts (us). */
    private long length() { return durationUs >= 0 ? durationUs : guessUs; }

    private long mediaNow() {
        long t = mediaUs + (System.currentTimeMillis() - startedAt) * 1000;
        return Math.min(t, length());
    }

    /** Starts the current play from mediaUs: its end timer and, for tones, the notes. */
    private void play() {
        int tk = ++token;
        long left = length() - mediaUs;
        MediaQueue.at(this, startedAt + Math.max(0, left) / 1000, tk);
        if (tones && seq != null && seq.n > 0) startTones(seq, mediaUs, tk);
    }

    /** Ends the current play (its timer and notes). */
    private void cut() {
        token++;
        if (tones) MediaQueue.tone(-1, 0, 0);
    }

    /** The media thread: the end of a play came. */
    synchronized void timer(int tk) {
        if (tk != token || state != STARTED) return;
        long end = length();
        if ((loopsLeft == -1 || loopsLeft > 1) && end > 0) {  // (nothing to play: no endless looping)
            if (loopsLeft > 1) loopsLeft--;
            mediaUs = 0;
            startedAt = System.currentTimeMillis();
            play();
        } else {
            state = PREFETCHED;
            mediaUs = end;
            atEnd = true;
            token++;
        }
        post(PlayerListener.END_OF_MEDIA, new Long(end));
    }

    private void startTones(final ToneSequence sq, final long fromUs, final int tk) {
        new Thread("tones") {
            public void run() {
                long t = 0;
                int i = 0;
                while (i < sq.n && t + sq.ms[i] * 1000L <= fromUs) t += sq.ms[i++] * 1000L;
                for (; i < sq.n && token == tk; i++) {
                    int v = volume.effective() * sq.vols[i] / 100;
                    if (sq.notes[i] >= 0 && v > 0) MediaQueue.tone(sq.notes[i], sq.ms[i], v);
                    try {
                        Thread.sleep(sq.ms[i]);
                    } catch (InterruptedException e) {
                    }
                }
            }
        }.start();
    }

    private PlayerListener[] listeners() {
        PlayerListener[] r = new PlayerListener[listeners.size()];
        listeners.copyInto(r);
        return r;
    }

    private void post(String ev, Object data) { MediaQueue.post(this, ev, data, listeners()); }

    final class Volume implements VolumeControl {
        private int level = 100;
        private boolean muted;

        public void setMute(boolean mute) {
            synchronized (BasePlayer.this) {
                if (mute == muted) return;
                muted = mute;
                if (state != CLOSED) post(PlayerListener.VOLUME_CHANGED, this);
            }
        }

        public boolean isMuted() { return muted; }

        public int setLevel(int l) {
            if (l < 0) l = 0;
            if (l > 100) l = 100;
            synchronized (BasePlayer.this) {
                if (l != level) {
                    level = l;
                    if (state != CLOSED) post(PlayerListener.VOLUME_CHANGED, this);
                }
            }
            return l;
        }

        public int getLevel() { return level; }

        int effective() { return muted ? 0 : level; }
    }

    final class Tone implements ToneControl {
        public void setSequence(byte[] sequence) {
            ToneSequence s = ToneSequence.parse(sequence);
            synchronized (BasePlayer.this) {
                if (state == STARTED) throw new IllegalStateException("started");
                open();
                seq = s;
                durationUs = s.durationUs;
                mediaUs = 0;
                atEnd = false;
                loopsLeft = loopCount;
                post(PlayerListener.DURATION_UPDATED, new Long(durationUs));
            }
        }
    }
}
