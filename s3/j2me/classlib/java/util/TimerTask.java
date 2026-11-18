package java.util;

// MIDP 2.0's TimerTask (run by java.util.Timer's thread).
public abstract class TimerTask implements Runnable {
  static final int VIRGIN = 0, SCHEDULED = 1, EXECUTED = 2, CANCELLED = 3;
  final Object lock = new Object();
  int state = VIRGIN;
  long next, period, last;
  boolean fixedRate;
  protected TimerTask() {}
  public abstract void run();
  public boolean cancel() {
    synchronized (lock) {
      boolean was = state == SCHEDULED;
      state = CANCELLED;
      return was;
    }
  }
  public long scheduledExecutionTime() { synchronized (lock) { return last; } }
}
