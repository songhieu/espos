// Exceptions: try/catch/finally in every shape, rethrow, nesting, custom classes, VM-raised exceptions, stack
// overflow, static initializer failures, monitors released by exceptions. Output compared with the JDK.
public class ExceptionTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }
  static StringBuffer log = new StringBuffer();

  static class MyException extends Exception {
    int code;
    MyException(String m, int code) { super(m); this.code = code; }
  }
  static class SubException extends MyException {
    SubException(String m) { super(m, 2); }
  }
  static class Boom {
    static int value;
    static { value = 1; if (value == 1) throw new IllegalStateException("static boom"); }
  }
  static class BadSize {
    static int[] arr = new int[-1];
  }

  static int finallyReturn() {
    try { return 1; } finally { log.append("F"); }
  }
  static int finallyOverrides() {
    try { throw new RuntimeException("lost"); } finally { return 2; }
  }
  static int nested(int depth) throws MyException {
    try {
      if (depth == 0) throw new SubException("deep");
      return nested(depth - 1);
    } finally {
      log.append(depth);
    }
  }
  static void rethrow() throws MyException {
    try { nested(3); } catch (MyException e) { log.append("R"); throw e; }
  }
  static int recurse(int n) { return n <= 0 ? 0 : 1 + recurse(n - 1) + 0 * recurse(-1); }
  static int forever(int n) { return forever(n + 1) + 1; }
  static int loopFinally() {
    int n = 0;
    for (int i = 0; i < 5; i++) {
      try {
        if (i == 1) continue;
        if (i == 3) break;
        n += 10;
      } finally {
        n++;
      }
    }
    return n;
  }
  static String npeKind(int k) {
    Object o = null;
    String s = null;
    int[] a = null;
    ExceptionTest t = null;
    try {
      switch (k) {
        case 0: o.hashCode(); break;
        case 1: s.length(); break;
        case 2: a[0]++; break;
        case 3: p(t.toString()); break;
        case 4: synchronized (o) { p("in"); } break;
        case 5: throw null;
        case 6: p("" + t.inst); break;
        default: return "none";
      }
    } catch (NullPointerException e) {
      return "npe" + k;
    }
    return "no npe " + k;
  }
  int inst;

  public static void main(String[] args) throws Exception {
    p("finally " + finallyReturn() + " " + finallyOverrides() + " " + log);
    log.setLength(0);
    try { rethrow(); check(false, "rethrow"); } catch (SubException e) { p("caught " + e.getMessage() + " " + e.code + " " + log); }
    catch (MyException e) { check(false, "wrong catch"); }
    try { throw new MyException("mine", 7); } catch (Exception e) { p("super catch " + (e instanceof MyException) + " " + ((MyException) e).code + " " + e); }
    p("loopFinally " + loopFinally());
    // nested try in catch and finally
    log.setLength(0);
    try {
      try { throw new IllegalArgumentException("a"); }
      catch (IllegalArgumentException e) { log.append("1"); throw new IllegalStateException("b"); }
      finally { log.append("2"); }
    } catch (IllegalStateException e) { log.append("3" + e.getMessage()); }
    finally { log.append("4"); }
    p("nested " + log);
    // VM-raised exceptions and their types
    StringBuffer kinds = new StringBuffer();
    for (int k = 0; k < 8; k++) kinds.append(npeKind(k)).append(' ');
    p("npes " + kinds);
    try { Object x = "s"; Integer bad = (Integer) x; check(false, "cce " + bad); } catch (ClassCastException e) { p("cce " + (e instanceof RuntimeException)); }
    try { int[] arr = new int[1]; arr[1] = 0; } catch (IndexOutOfBoundsException e) { p("ioobe " + e.getClass().getName()); }
    try { "abc".substring(5); } catch (IndexOutOfBoundsException e) { p("sioobe " + e.getClass().getName()); }
    try { throw new Error("err"); } catch (Throwable t) { p("throwable " + t.getMessage() + " " + (t instanceof Error)); }
    // stack overflow is catchable and the thread goes on
    p("recurse " + recurse(500));
    try { forever(0); check(false, "no overflow"); } catch (StackOverflowError e) { p("soe ok"); }
    p("after soe " + recurse(100));
    // static initializer failure: ExceptionInInitializerError, then NoClassDefFoundError
    try { p("v " + Boom.value); check(false, "Boom"); } catch (ExceptionInInitializerError e) { p("eiie ok"); }
    try { p("v " + Boom.value); check(false, "Boom again"); } catch (NoClassDefFoundError e) { p("ncdfe ok"); }
    try { p("len " + BadSize.arr.length); check(false, "BadSize"); } catch (ExceptionInInitializerError e) { p("eiie2 ok"); }
    // a missing class
    try { Class.forName("no.such.Thing"); check(false, "forName"); } catch (ClassNotFoundException e) { p("cnfe " + e.getMessage()); }
    // an exception thrown inside synchronized releases the monitor (another thread can take it)
    final Object lock = new Object();
    try { synchronized (lock) { throw new RuntimeException("in sync"); } } catch (RuntimeException e) { p("sync ex " + e.getMessage()); }
    final boolean[] got = {false};
    Thread th = new Thread() { public void run() { synchronized (lock) { got[0] = true; } } };
    th.start();
    th.join();
    p("monitor released " + got[0]);
    // exception in a synchronized method
    try { syncThrow(); } catch (IllegalStateException e) { p("sync method ex"); }
    Thread th2 = new Thread() { public void run() { syncNothing(); } };
    th2.start();
    th2.join();
    p("class monitor released " + syncRan);
    // toString/getMessage of exceptions without a message
    p("msgs " + new RuntimeException().getMessage() + " " + new RuntimeException() + " " + new MyException(null, 1));
    // an uncaught exception kills only its thread
    Thread dying = new Thread() { public void run() { throw new RuntimeException("expected: this thread dies"); } };
    dying.start();
    dying.join();
    p("survived " + dying.isAlive());
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
  static boolean syncRan;
  static synchronized void syncThrow() { throw new IllegalStateException(); }
  static synchronized void syncNothing() { syncRan = true; }
}
