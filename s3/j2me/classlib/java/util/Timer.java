package java.util;

// MIDP 2.0's Timer: one background thread runs the tasks in time order. An exception thrown by a task ends the
// timer (as in J2SE).
public class Timer {
  private final Vector tasks = new Vector();  // TimerTasks, earliest first
  private boolean cancelled;

  public Timer() {
    Thread t = new Thread("Timer") {
      public void run() { loop(); }
    };
    t.start();
  }

  public void schedule(TimerTask task, long delay) {
    if (delay < 0) throw new IllegalArgumentException("Negative delay.");
    add(task, System.currentTimeMillis() + delay, 0, false);
  }
  public void schedule(TimerTask task, Date time) { add(task, time.getTime(), 0, false); }
  public void schedule(TimerTask task, long delay, long period) {
    if (delay < 0) throw new IllegalArgumentException("Negative delay.");
    if (period <= 0) throw new IllegalArgumentException("Non-positive period.");
    add(task, System.currentTimeMillis() + delay, period, false);
  }
  public void schedule(TimerTask task, Date first, long period) {
    if (period <= 0) throw new IllegalArgumentException("Non-positive period.");
    add(task, first.getTime(), period, false);
  }
  public void scheduleAtFixedRate(TimerTask task, long delay, long period) {
    if (delay < 0) throw new IllegalArgumentException("Negative delay.");
    if (period <= 0) throw new IllegalArgumentException("Non-positive period.");
    add(task, System.currentTimeMillis() + delay, period, true);
  }
  public void scheduleAtFixedRate(TimerTask task, Date first, long period) {
    if (period <= 0) throw new IllegalArgumentException("Non-positive period.");
    add(task, first.getTime(), period, true);
  }
  public void cancel() {
    synchronized (tasks) {
      cancelled = true;
      tasks.removeAllElements();
      tasks.notify();
    }
  }

  private void add(TimerTask task, long time, long period, boolean fixedRate) {
    if (time < 0) throw new IllegalArgumentException("Illegal execution time.");
    synchronized (tasks) {
      if (cancelled) throw new IllegalStateException("Timer already cancelled.");
      synchronized (task.lock) {
        if (task.state != TimerTask.VIRGIN) throw new IllegalStateException("Task already scheduled or cancelled");
        task.next = time;
        task.period = period;
        task.fixedRate = fixedRate;
        task.state = TimerTask.SCHEDULED;
      }
      insert(task);
      tasks.notify();
    }
  }

  private void insert(TimerTask task) {
    int i = 0, n = tasks.size();
    while (i < n && ((TimerTask) tasks.elementAt(i)).next <= task.next) i++;
    tasks.insertElementAt(task, i);
  }

  private void loop() {
    for (;;) {
      TimerTask task;
      synchronized (tasks) {
        while (tasks.isEmpty() && !cancelled) {
          try { tasks.wait(); } catch (InterruptedException e) {}
        }
        if (cancelled) return;
        task = (TimerTask) tasks.elementAt(0);
        long now = System.currentTimeMillis();
        boolean fire;
        synchronized (task.lock) {
          if (task.state == TimerTask.CANCELLED) {
            tasks.removeElementAt(0);
            continue;
          }
          fire = task.next <= now;
          if (fire) {
            tasks.removeElementAt(0);
            task.last = task.next;
            if (task.period == 0) {
              task.state = TimerTask.EXECUTED;
            } else {
              task.next = task.fixedRate ? task.next + task.period : now + task.period;
              insert(task);
            }
          }
        }
        if (!fire) {
          try { tasks.wait(task.next - now); } catch (InterruptedException e) {}
          continue;
        }
      }
      try {
        task.run();
      } catch (RuntimeException e) {
        synchronized (tasks) {
          cancelled = true;
          tasks.removeAllElements();
        }
        throw e;
      }
    }
  }
}
