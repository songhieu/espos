import java.util.Timer;
import java.util.TimerTask;

// Green threads: synchronized counters, wait/notify producer/consumer, sleep timing, join, interrupt, wait timeouts,
// Timer, many short threads. Timing-dependent: run.sh checks only "ALL OK".
public class ThreadTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }

  static int counter;
  static synchronized void inc() { counter++; }

  static class Buffer {
    private final int[] items = new int[4];
    private int head, count;
    synchronized void put(int v) throws InterruptedException {
      while (count == items.length) wait();
      items[(head + count) % items.length] = v;
      count++;
      notifyAll();
    }
    synchronized int take() throws InterruptedException {
      while (count == 0) wait();
      int v = items[head];
      head = (head + 1) % items.length;
      count--;
      notifyAll();
      return v;
    }
  }

  public static void main(String[] args) throws Exception {
    // 4 threads x 5000 increments through a synchronized static method and a synchronized block
    final Object lock = new Object();
    final int[] blockCount = {0};
    Thread[] ts = new Thread[4];
    for (int i = 0; i < ts.length; i++) {
      ts[i] = new Thread() {
        public void run() {
          for (int k = 0; k < 5000; k++) {
            inc();
            synchronized (lock) { blockCount[0]++; }
            if (k % 500 == 0) Thread.yield();
          }
        }
      };
      ts[i].start();
    }
    for (int i = 0; i < ts.length; i++) ts[i].join();
    check(counter == 20000 && blockCount[0] == 20000, "counters " + counter + " " + blockCount[0]);
    p("counters " + counter + " " + blockCount[0]);
    // producer/consumer through wait/notifyAll on a 4-slot buffer
    final Buffer buf = new Buffer();
    final long[] got = {0, 0};
    Thread consumer = new Thread() {
      public void run() {
        try {
          for (int i = 0; i < 1000; i++) {
            int v = buf.take();
            if (v != i) got[1]++;
            got[0] += v;
          }
        } catch (InterruptedException e) { got[1] = -1; }
      }
    };
    consumer.start();
    for (int i = 0; i < 1000; i++) buf.put(i);
    consumer.join();
    check(got[0] == 499500 && got[1] == 0, "producer/consumer " + got[0] + " " + got[1]);
    p("prodcons " + got[0] + " " + got[1]);
    // sleep timing (does not burn the other threads' time)
    long t0 = System.currentTimeMillis();
    Thread.sleep(60);
    long dt = System.currentTimeMillis() - t0;
    check(dt >= 59 && dt < 400, "sleep(60) took " + dt);
    // wait with a timeout returns by itself
    t0 = System.currentTimeMillis();
    synchronized (lock) { lock.wait(40); }
    dt = System.currentTimeMillis() - t0;
    check(dt >= 39 && dt < 400, "wait(40) took " + dt);
    // join on a sleeping thread, isAlive
    Thread sleeper = new Thread() { public void run() { try { Thread.sleep(30); } catch (InterruptedException e) {} } };
    check(!sleeper.isAlive(), "alive before start");
    sleeper.start();
    check(sleeper.isAlive(), "alive after start");
    sleeper.join();
    check(!sleeper.isAlive(), "alive after join");
    try { sleeper.start(); check(false, "start twice"); } catch (IllegalThreadStateException e) {}
    // interrupt a sleeping thread and a waiting one
    final int[] intr = {0};
    Thread s2 = new Thread() { public void run() { try { Thread.sleep(10000); } catch (InterruptedException e) { intr[0]++; } } };
    s2.start();
    Thread.sleep(10);
    s2.interrupt();
    s2.join();
    final Object w = new Object();
    Thread w2 = new Thread() { public void run() { synchronized (w) { try { w.wait(); } catch (InterruptedException e) { intr[0] += 10; } } } };
    w2.start();
    Thread.sleep(10);
    w2.interrupt();
    w2.join();
    check(intr[0] == 11, "interrupts " + intr[0]);
    // wait/notify handshake, notify without the monitor
    try { w.notify(); check(false, "notify without monitor"); } catch (IllegalMonitorStateException e) {}
    try { w.wait(); check(false, "wait without monitor"); } catch (IllegalMonitorStateException e) {}
    // currentThread, names, priorities
    Thread me = Thread.currentThread();
    check(me.getName().equals("main"), "main name " + me.getName());
    final String[] seen = {null};
    Thread named = new Thread("worker") { public void run() { seen[0] = Thread.currentThread().getName(); } };
    named.setPriority(Thread.MAX_PRIORITY);
    named.start();
    named.join();
    check("worker".equals(seen[0]), "worker name " + seen[0]);
    // Timer: one-shot and periodic tasks
    final int[] ticks = {0, 0};
    Timer timer = new Timer();
    timer.schedule(new TimerTask() { public void run() { ticks[0]++; } }, 20);
    TimerTask periodic = new TimerTask() { public void run() { ticks[1]++; } };
    timer.scheduleAtFixedRate(periodic, 0, 10);
    Thread.sleep(75);
    periodic.cancel();
    int after = ticks[1];
    Thread.sleep(40);
    check(ticks[0] == 1, "one-shot timer " + ticks[0]);
    check(after >= 5 && after <= 10 && ticks[1] == after, "periodic timer " + after + " " + ticks[1]);
    timer.cancel();
    // 200 short-lived threads (the VM frees finished ones)
    final int[] done = {0};
    for (int i = 0; i < 200; i++) {
      Thread x = new Thread() { public void run() { synchronized (done) { done[0]++; } } };
      x.start();
      if (i % 50 == 49) x.join();
    }
    while (true) { synchronized (done) { if (done[0] == 200) break; } Thread.sleep(1); }
    check(done[0] == 200, "short threads " + done[0]);
    p("activeCount " + (Thread.activeCount() >= 1));
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
