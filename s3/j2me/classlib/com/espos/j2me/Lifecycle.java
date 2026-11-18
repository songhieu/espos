package com.espos.j2me;

/** The MIDlet's lifecycle, called by the display loop (javax.microedition.lcdui.DisplayHost). */
public interface Lifecycle {
    void pause();
    void resume();
    void destroy();
}
