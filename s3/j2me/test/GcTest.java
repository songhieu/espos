// The collector under pressure (run.sh gives it a 2 MB heap): ~60 MB allocated in small objects and big arrays while
// a linked list, a Hashtable, statics, locals and other threads' stacks keep live data that must survive intact.
// Then OutOfMemoryError is caught and the VM goes on.
import java.util.Hashtable;
import java.util.Vector;

public class GcTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }

  static class Node {
    int value;
    int[] payload;
    Node next;
    Node(int v, Node n) { value = v; payload = new int[] {v, v * 2, v * 3}; next = n; }
  }
  static Object[] statics = new Object[16];

  static long churn(int rounds) {
    long total = 0;
    for (int r = 0; r < rounds; r++) {
      int[] small = new int[8];          // 48 bytes
      small[7] = r;
      total += small.length * 4 + 16;
      if (r % 50 == 0) {
        byte[] big = new byte[64 * 1024];  // 64 KB
        big[big.length - 1] = (byte) r;
        total += big.length;
      }
      if (r % 7 == 0) { String s = "s" + r; total += s.length() * 2 + 40; }
    }
    return total;
  }

  public static void main(String[] args) throws Exception {
    // live data: a 5000-node list (~350 KB), statics, a Hashtable
    Node head = null;
    for (int i = 0; i < 5000; i++) head = new Node(i, head);
    for (int i = 0; i < statics.length; i++) statics[i] = new long[] {i, -i};
    Hashtable h = new Hashtable();
    for (int i = 0; i < 300; i++) h.put("k" + i, new int[] {i});
    // another thread holds live data on its stack while it sleeps and churns
    final boolean[] ok = {false};
    Thread other = new Thread() {
      public void run() {
        int[] mine = new int[1000];
        for (int i = 0; i < mine.length; i++) mine[i] = i * i;
        String text = "kept " + mine.length;
        for (int k = 0; k < 20; k++) {
          churn(2000);
          try { Thread.sleep(1); } catch (InterruptedException e) {}
        }
        long s = 0;
        for (int i = 0; i < mine.length; i++) s += mine[i];
        ok[0] = s == 332833500L && text.equals("kept 1000");
      }
    };
    other.start();
    long total = 0;
    for (int k = 0; k < 20; k++) total += churn(2500);
    other.join();
    check(ok[0], "other thread's live data");
    // verify the list
    int count = 0, expect = 4999;
    boolean good = true;
    for (Node n = head; n != null; n = n.next, expect--) {
      count++;
      if (n.value != expect || n.payload.length != 3 || n.payload[2] != expect * 3) good = false;
    }
    check(good && count == 5000, "linked list " + count);
    for (int i = 0; i < statics.length; i++) check(((long[]) statics[i])[1] == -i, "static " + i);
    for (int i = 0; i < 300; i++) check(((int[]) h.get("k" + i))[0] == i, "hashtable " + i);
    check(total > 50L * 1024 * 1024, "allocated " + total);
    // a Vector of big arrays that cannot all fit: OutOfMemoryError is thrown, caught, and memory comes back
    Vector keep = new Vector();
    boolean oom = false;
    try {
      for (int i = 0; i < 100; i++) keep.addElement(new byte[256 * 1024]);
    } catch (OutOfMemoryError e) {
      oom = true;
    }
    int held = keep.size();
    keep = null;
    check(oom && held > 2 && held < 100, "OutOfMemoryError after " + held);
    try { byte[] huge = new byte[Integer.MAX_VALUE]; check(false, "huge " + huge.length); } catch (OutOfMemoryError e) {}
    byte[] again = new byte[512 * 1024];
    again[0] = 1;
    Runtime rt = Runtime.getRuntime();
    System.gc();
    check(rt.freeMemory() > 0 && rt.totalMemory() >= rt.freeMemory(), "Runtime memory");
    p("allocated MB " + (total >> 20) + ", held " + held + " x 256 KB before OOM, free " + (rt.freeMemory() >> 10) + " KB");
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
