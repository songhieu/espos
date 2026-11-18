package java.lang;

// The class library of EspOS's J2ME VM (src/j2me): CLDC 1.1's java.lang, written for it; natives in vm_natives.cpp.
public class Object {
  public Object() {}
  public final native Class getClass();
  public native int hashCode();
  public boolean equals(Object o) { return this == o; }
  // arrays only (CLDC has no Cloneable): javac calls it for array.clone()
  protected native Object clone() throws CloneNotSupportedException;
  public String toString() { return getClass().getName() + "@" + Integer.toHexString(hashCode()); }
  public final native void notify();
  public final native void notifyAll();
  public final native void wait(long ms) throws InterruptedException;
  public final void wait(long ms, int nanos) throws InterruptedException {
    if (nanos < 0 || nanos > 999999) throw new IllegalArgumentException("nanos");
    wait(ms > 0 || nanos == 0 ? ms : 1);
  }
  public final void wait() throws InterruptedException { wait(0); }
}
