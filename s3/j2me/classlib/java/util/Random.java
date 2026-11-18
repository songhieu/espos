package java.util;

public class Random {
  private long seed;
  public Random() { this(System.currentTimeMillis()); }
  public Random(long s) { setSeed(s); }
  public synchronized void setSeed(long s) { seed = (s ^ 0x5DEECE66DL) & ((1L << 48) - 1); }
  protected synchronized int next(int bits) {
    seed = (seed * 0x5DEECE66DL + 0xBL) & ((1L << 48) - 1);
    return (int) (seed >>> (48 - bits));
  }
  public int nextInt() { return next(32); }
  public int nextInt(int n) {
    if (n <= 0) throw new IllegalArgumentException();
    if ((n & -n) == n) return (int) ((n * (long) next(31)) >> 31);
    int b, v;
    do { b = next(31); v = b % n; } while (b - v + (n - 1) < 0);
    return v;
  }
  public long nextLong() { return ((long) next(32) << 32) + next(32); }
  public float nextFloat() { return next(24) / ((float) (1 << 24)); }
  public double nextDouble() { return (((long) next(26) << 27) + next(27)) / (double) (1L << 53); }
}
