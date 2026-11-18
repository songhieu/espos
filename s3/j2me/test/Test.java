import java.util.*;

// The VM's first test (src/j2me, tools/j2me): language features, the class library, threads, then a timed workload.
public class Test {
  interface Shape { int area(); }
  static class Rect implements Shape { int w, h; Rect(int w, int h) { this.w = w; this.h = h; } public int area() { return w * h; } }
  static class Square extends Rect { Square(int s) { super(s, s); } public String toString() { return "Square " + w; } }
  static int counter;
  static final int[] PRIMES;
  static { PRIMES = new int[] {2, 3, 5, 7, 11}; }

  static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

  static int sieve(int n) {
    boolean[] c = new boolean[n + 1];
    int k = 0;
    for (int i = 2; i <= n; i++) {
      if (c[i]) continue;
      k++;
      for (int j = i * 2; j <= n; j += i) c[j] = true;
    }
    return k;
  }

  public static void main(String[] args) throws Exception {
    int fails = 0;
    // arithmetic, longs, floats
    long big = 1L << 40;
    if (big / 1024 != 1073741824L) { System.out.println("FAIL long"); fails++; }
    float f = 1.5f * 3;
    if (f != 4.5f) { System.out.println("FAIL float"); fails++; }
    double d = Math.sqrt(2.0);
    if (d < 1.414 || d > 1.415) { System.out.println("FAIL sqrt " + d); fails++; }
    if ((-7 / 2) != -3 || (-7 % 2) != -1 || (7 >>> 1) != 3 || (-8 >> 1) != -4) { System.out.println("FAIL int ops"); fails++; }
    // strings
    String s = "Hello" + ", " + "J2ME " + 42 + " " + true + " " + 'x';
    if (!s.equals("Hello, J2ME 42 true x")) { System.out.println("FAIL concat [" + s + "]"); fails++; }
    StringBuffer sb = new StringBuffer();
    for (int i = 0; i < 5; i++) sb.append(i).append(',');
    if (!sb.toString().equals("0,1,2,3,4,")) { System.out.println("FAIL sb"); fails++; }
    if (!"abc".toUpperCase().equals("ABC") || "hello".indexOf("ll") != 2 || Integer.parseInt("-123") != -123) { System.out.println("FAIL string ops"); fails++; }
    // objects, interfaces, instanceof, casts
    Shape[] shapes = {new Rect(2, 3), new Square(4)};
    int area = 0;
    for (int i = 0; i < shapes.length; i++) area += shapes[i].area();
    if (area != 22 || !(shapes[1] instanceof Rect) || !shapes[1].toString().equals("Square 4")) { System.out.println("FAIL objects"); fails++; }
    // exceptions
    try { Object o = "x"; Integer bad = (Integer) o; fails++; System.out.println("FAIL no ClassCast"); } catch (ClassCastException e) {}
    try { int[] a = new int[2]; a[3] = 1; fails++; } catch (ArrayIndexOutOfBoundsException e) {}
    try { String n = null; n.length(); fails++; } catch (NullPointerException e) {}
    try { int z = 0; z = 5 / z; fails++; } catch (ArithmeticException e) {}
    try { throw new IllegalStateException("mine"); } catch (RuntimeException e) { if (!e.getMessage().equals("mine")) fails++; }
    // statics, arrays, 2D arrays
    int[][] grid = new int[3][4];
    grid[2][3] = 9;
    if (PRIMES[4] != 11 || grid[2][3] != 9 || grid.length != 3 || grid[0].length != 4) { System.out.println("FAIL arrays"); fails++; }
    // collections
    Vector v = new Vector();
    Hashtable h = new Hashtable();
    for (int i = 0; i < 100; i++) { v.addElement(new Integer(i)); h.put("k" + i, new Integer(i * i)); }
    if (v.size() != 100 || ((Integer) h.get("k9")).intValue() != 81 || ((Integer) v.elementAt(50)).intValue() != 50) { System.out.println("FAIL collections"); fails++; }
    // threads
    Thread[] ts = new Thread[3];
    for (int i = 0; i < 3; i++) {
      ts[i] = new Thread(new Runnable() { public void run() { for (int k = 0; k < 1000; k++) { synchronized (Test.class) { counter++; } if (k % 100 == 0) Thread.yield(); } } });
      ts[i].start();
    }
    for (int i = 0; i < 3; i++) ts[i].join();
    if (counter != 3000) { System.out.println("FAIL threads " + counter); fails++; }
    long t0 = System.currentTimeMillis();
    Thread.sleep(50);
    long slept = System.currentTimeMillis() - t0;
    if (slept < 45) { System.out.println("FAIL sleep " + slept); fails++; }
    System.out.println(fails == 0 ? "ALL OK" : fails + " FAILED");
    // timed workload
    t0 = System.currentTimeMillis();
    int fb = fib(25);
    long t1 = System.currentTimeMillis();
    int pr = sieve(200000);
    long t2 = System.currentTimeMillis();
    StringBuffer w = new StringBuffer();
    for (int i = 0; i < 20000; i++) { w.setLength(0); w.append("item").append(i); h.put(w.toString(), v); }
    long t3 = System.currentTimeMillis();
    System.out.println("fib(25)=" + fb + " " + (t1 - t0) + " ms, sieve(200000)=" + pr + " " + (t2 - t1) + " ms, strings+hash " + (t3 - t2) + " ms");
  }
}
