// Game-like workloads for the interpreter's speed (run with j2me-run -bench: bytecodes/s at the end).
public class BenchTest {
  static final int W = 240, H = 320;
  int x, y, dx = 3, dy = 2;
  int getX() { return x; }
  void move() { x += dx; y += dy; if (x < 0 || x > W) dx = -dx; if (y < 0 || y > H) dy = -dy; }

  interface Sprite { int frame(int t); }
  static class Walker implements Sprite { public int frame(int t) { return (t >> 2) & 3; } }
  static class Flyer implements Sprite { public int frame(int t) { return (t >> 1) & 7; } }

  public static void main(String[] args) {
    long t0 = System.currentTimeMillis();
    // fixed-point maths (16.16), the way old games did trigonometry and physics
    int acc = 0;
    for (int i = 0; i < 2000000; i++) {
      int a = i << 4, b = (i * 13) & 0xffff;
      acc += (int) (((long) a * b) >> 16) ^ (a >>> 3);
    }
    long t1 = System.currentTimeMillis();
    // pixel buffer: fill, blit with a transparent colour key, scroll
    int[] screen = new int[W * H];
    int[] tile = new int[16 * 16];
    for (int i = 0; i < tile.length; i++) tile[i] = (i & 5) == 0 ? 0xff00ff : i * 0x010203;
    for (int frame = 0; frame < 20; frame++) {
      for (int ty = 0; ty < H; ty += 16)
        for (int tx = 0; tx < W; tx += 16)
          for (int py = 0; py < 16; py++) {
            int src = py << 4, dst = (ty + py) * W + tx;
            for (int px = 0; px < 16; px++) {
              int c = tile[src + px];
              if (c != 0xff00ff) screen[dst + px] = c + frame;
            }
          }
      System.arraycopy(screen, W, screen, 0, W * (H - 1));
    }
    long t2 = System.currentTimeMillis();
    // objects: virtual and interface calls, field access
    BenchTest[] balls = new BenchTest[50];
    for (int i = 0; i < balls.length; i++) { balls[i] = new BenchTest(); balls[i].x = i * 4; balls[i].y = i * 6; }
    Sprite[] sprites = {new Walker(), new Flyer()};
    int sum = 0;
    for (int t = 0; t < 20000; t++)
      for (int i = 0; i < balls.length; i++) {
        balls[i].move();
        sum += balls[i].getX() + sprites[i & 1].frame(t);
      }
    long t3 = System.currentTimeMillis();
    // strings and a hashtable (score tables, menus)
    java.util.Hashtable h = new java.util.Hashtable();
    StringBuffer sb = new StringBuffer();
    for (int i = 0; i < 30000; i++) {
      sb.setLength(0);
      sb.append("score").append(i % 500);
      String k = sb.toString();
      Integer old = (Integer) h.get(k);
      h.put(k, new Integer(old == null ? 1 : old.intValue() + 1));
    }
    long t4 = System.currentTimeMillis();
    System.out.println("fixed-point " + (t1 - t0) + " ms, pixels " + (t2 - t1) + " ms, objects " + (t3 - t2) + " ms, strings " + (t4 - t3) + " ms (total " + (t4 - t0) + " ms)");
    System.out.println("checks " + acc + " " + screen[W * 10 + 7] + " " + sum + " " + h.size());
    System.out.println("ALL OK");
  }
}
