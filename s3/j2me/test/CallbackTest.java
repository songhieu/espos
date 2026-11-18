// Natives that call back into Java (Vm::call), the way the MIDP layer calls paint()/keyPressed(): the natives are in
// tools/j2me/main.cpp. Covers: a nested call running to its end, an exception coming out of it, Thread.sleep and a
// contended monitor and wait() inside the nested call (other threads run meanwhile), a call on a separate host
// thread (newThread), long/double arguments, and objects a native allocates staying alive across GCs.
public class CallbackTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }

  static native void callRun(Runnable r);        // r.run() nested on this thread
  static native void callOnHost(Runnable r);     // r.run() on the host thread "event"
  static native double callAdd(int a, long b, double c);  // calls add(int, long, double) through Vm::call
  static native Object[] allocMany(int n);       // n strings "s<i>", allocated by the native
  static double add(int a, long b, double c) { return a + b + c; }

  static volatile int ticks;
  static int count;

  public static void main(String[] args) throws Exception {
    // 1. runs to its end
    callRun(new Runnable() { public void run() { count++; } });
    check(count == 1, "nested call ran " + count);
    // 2. an exception comes out of the native
    try {
      callRun(new Runnable() { public void run() { throw new IllegalStateException("from callback"); } });
      check(false, "exception lost");
    } catch (IllegalStateException e) {
      check(e.getMessage().equals("from callback"), "exception message");
    }
    // 3. sleep inside the callback: another thread keeps running
    Thread ticker = new Thread() {
      public void run() {
        for (int i = 0; i < 20; i++) { ticks++; try { Thread.sleep(2); } catch (InterruptedException e) {} }
      }
    };
    ticker.start();
    final int[] seenTicks = {0};
    callRun(new Runnable() {
      public void run() {
        try { Thread.sleep(60); } catch (InterruptedException e) {}
        seenTicks[0] = ticks;
      }
    });
    check(seenTicks[0] >= 15, "other thread ran during the nested sleep: " + seenTicks[0]);
    ticker.join();
    // 4. a monitor held by another thread: the callback waits for it
    final Object lock = new Object();
    final StringBuffer order = new StringBuffer();
    Thread holder = new Thread() {
      public void run() {
        synchronized (lock) {
          order.append("H");
          try { Thread.sleep(30); } catch (InterruptedException e) {}
          order.append("h");
        }
      }
    };
    holder.start();
    Thread.sleep(5);  // let it take the lock
    callRun(new Runnable() { public void run() { synchronized (lock) { order.append("C"); } } });
    check(order.toString().equals("HhC"), "monitor order " + order);
    holder.join();
    // 5. wait() inside the callback, notified by another thread
    final Object signal = new Object();
    final boolean[] notified = {false};
    Thread notifier = new Thread() {
      public void run() {
        try { Thread.sleep(20); } catch (InterruptedException e) {}
        synchronized (signal) { notified[0] = true; signal.notifyAll(); }
      }
    };
    notifier.start();
    callRun(new Runnable() {
      public void run() {
        synchronized (signal) {
          while (!notified[0]) { try { signal.wait(); } catch (InterruptedException e) {} }
        }
      }
    });
    check(notified[0], "wait in callback");
    // 6. a separate host thread
    final String[] name = {null};
    callOnHost(new Runnable() { public void run() { name[0] = Thread.currentThread().getName(); } });
    check("event".equals(name[0]), "host thread name " + name[0]);
    // 7. long/double arguments through Vm::call
    double r = callAdd(1, 1L << 40, 0.5);
    check(r == 1 + (double) (1L << 40) + 0.5, "callAdd " + r);
    // 8. what a native allocates stays alive while it allocates more (run.sh gives a 4 MB heap: ~1.5 MB per round)
    for (int round = 0; round < 5; round++) {
      Object[] a = allocMany(20000);
      boolean ok = a.length == 20000;
      for (int i = 0; ok && i < a.length; i++) ok = ("s" + i).equals(a[i]);
      check(ok, "allocMany round " + round);
    }
    p("callbacks: ticks seen " + seenTicks[0] + ", order " + order);
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
