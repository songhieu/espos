package java.lang;

// Green threads (src/j2me): the VM switches between them; sleep, yield, join and wait give the turn away.
public class Thread implements Runnable {
  public static final int MIN_PRIORITY = 1, NORM_PRIORITY = 5, MAX_PRIORITY = 10;
  private Runnable target;
  private String name;
  private int priority = NORM_PRIORITY;
  private int vmThread;  // the VM's thread id (src/j2me), 0 = not started
  public Thread() { this(null, null); }
  public Thread(Runnable r) { this(r, null); }
  public Thread(String n) { this(null, n); }
  private static int made;
  public Thread(Runnable r, String n) { target = r; name = n != null ? n : "Thread-" + made++; }
  public void run() { if (target != null) target.run(); }
  public synchronized native void start();
  public static native Thread currentThread();
  public static native void sleep(long ms) throws InterruptedException;
  public static native void yield();
  public static native int activeCount();
  public final native boolean isAlive();
  public native void interrupt();
  public final void setPriority(int p) {
    if (p < MIN_PRIORITY || p > MAX_PRIORITY) throw new IllegalArgumentException();
    priority = p;
  }
  public final int getPriority() { return priority; }
  public final String getName() { return name; }
  // the VM notifies the Thread object when the thread ends
  public final void join() throws InterruptedException { synchronized (this) { while (isAlive()) wait(0); } }
  public String toString() { return "Thread[" + name + "," + priority + "]"; }
}
