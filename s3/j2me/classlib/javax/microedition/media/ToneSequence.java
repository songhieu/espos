package javax.microedition.media;

import javax.microedition.media.control.ToneControl;

/**
 * A MIDP tone sequence (ToneControl), checked and laid out flat: note, length (ms) and volume of each tone.
 *   sequence = VERSION 1 [TEMPO t] [RESOLUTION r] {BLOCK_START b events BLOCK_END b} events
 *   event    = note length | PLAY_BLOCK b | SET_VOLUME v | REPEAT m note length
 * A tone lasts length * 60000 / (r * t) ms (tempo 4t beats a minute, r units a whole note; defaults t 30, r 64).
 */
final class ToneSequence {
    private static final int MAX_TONES = 20000;   // tones kept for playing; the length still counts them all
    private static final long MAX_WALK = 200000;  // tones walked at most (blocks can nest into huge numbers)

    int n;
    int[] notes = new int[16], ms = new int[16], vols = new int[16];
    long durationUs;

    private final byte[] s;
    private final int[] blockStart = new int[128], blockEnd = new int[128];
    private int tempo = 30, res = 64, volume = 100;
    private long walked;

    private ToneSequence(byte[] s) {
        this.s = s;
        for (int i = 0; i < 128; i++) blockStart[i] = -1;
    }

    /** IllegalArgumentException when it is not a tone sequence. */
    static ToneSequence parse(byte[] seq) {
        if (seq == null) throw new IllegalArgumentException("null sequence");
        ToneSequence t = new ToneSequence(seq);
        t.read();
        return t;
    }

    private int at(int p) {
        if (p >= s.length) throw new IllegalArgumentException("tone sequence ends early");
        return s[p];
    }

    private void read() {
        if (at(0) != ToneControl.VERSION || at(1) != 1) throw new IllegalArgumentException("tone sequence version");
        int p = 2;
        if (p < s.length && s[p] == ToneControl.TEMPO) {
            tempo = at(p + 1);
            if (tempo < 5) throw new IllegalArgumentException("tempo");
            p += 2;
        }
        if (p < s.length && s[p] == ToneControl.RESOLUTION) {
            res = at(p + 1);
            if (res < 1) throw new IllegalArgumentException("resolution");
            p += 2;
        }
        int events = 0;
        while (p < s.length) {
            if (s[p] == ToneControl.BLOCK_START) {
                int b = at(p + 1);
                if (b < 0) throw new IllegalArgumentException("block number");
                int q = check(p + 2, b);
                if (at(q + 1) != b) throw new IllegalArgumentException("block end");
                blockStart[b] = p + 2;
                blockEnd[b] = q;
                p = q + 2;
            } else {
                int q = step(p, true);
                events++;
                p = q;
            }
        }
        if (events == 0) throw new IllegalArgumentException("no tones");
        // lay it out: walk the top level events again, from the start
        volume = 100;
        p = 2;
        if (s[p] == ToneControl.TEMPO) p += 2;
        if (p < s.length && s[p] == ToneControl.RESOLUTION) p += 2;
        while (p < s.length) {
            if (s[p] == ToneControl.BLOCK_START) p = check(p + 2, s[p + 1]) + 2;  // a definition: skip it
            else p = step(p, false);
        }
    }

    /** Checks the events of block b from p up to its BLOCK_END; returns that BLOCK_END's position. */
    private int check(int p, int b) {
        for (;;) {
            int c = at(p);
            if (c == ToneControl.BLOCK_END) return p;
            if (c == ToneControl.BLOCK_START) throw new IllegalArgumentException("block inside a block");
            if (c == ToneControl.PLAY_BLOCK && at(p + 1) == b) throw new IllegalArgumentException("block plays itself");
            p = step(p, true);
        }
    }

    /** One event at p (only checked when dry, else played into the lists); returns the next position. */
    private int step(int p, boolean dry) {
        int c = at(p);
        if (c >= ToneControl.SILENCE) {
            tone(c, at(p + 1), 1, dry);
            return p + 2;
        }
        int v = at(p + 1);
        switch (c) {
            case ToneControl.PLAY_BLOCK:
                if (v < 0 || blockStart[v] < 0) throw new IllegalArgumentException("block " + v + " not defined");
                if (!dry) {
                    for (int q = blockStart[v]; q < blockEnd[v]; ) q = step(q, false);
                }
                return p + 2;
            case ToneControl.SET_VOLUME:
                if (v < 0 || v > 100) throw new IllegalArgumentException("volume");
                if (!dry) volume = v;
                return p + 2;
            case ToneControl.REPEAT:
                if (v < 2) throw new IllegalArgumentException("repeat");
                int note = at(p + 2);
                if (note < ToneControl.SILENCE) throw new IllegalArgumentException("repeat needs a tone");
                tone(note, at(p + 3), v, dry);
                return p + 4;
        }
        throw new IllegalArgumentException("tone sequence code " + c);
    }

    private void tone(int note, int len, int times, boolean dry) {
        if (len < 1) throw new IllegalArgumentException("tone length");
        if (dry) return;
        long us = len * 60000000L / ((long) res * tempo);
        for (int i = 0; i < times && walked < MAX_WALK; i++, walked++) {
            durationUs += us;
            if (n >= MAX_TONES) continue;
            if (n == notes.length) {
                notes = grow(notes);
                ms = grow(ms);
                vols = grow(vols);
            }
            notes[n] = note;
            ms[n] = (int) ((us + 500) / 1000);
            vols[n] = volume;
            n++;
        }
    }

    private static int[] grow(int[] a) {
        int[] b = new int[a.length * 2];
        System.arraycopy(a, 0, b, 0, a.length);
        return b;
    }
}
