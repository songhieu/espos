// Arrays: every element type, multi-dimensional, arraycopy (overlaps, type checks), clone, covariance, exceptions.
public class ArrayTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }
  static int id(int x) { return x; }

  static String str(int[] a) {
    StringBuffer b = new StringBuffer("[");
    for (int i = 0; i < a.length; i++) { if (i > 0) b.append(','); b.append(a[i]); }
    return b.append(']').toString();
  }

  public static void main(String[] args) {
    boolean[] z = new boolean[3];
    byte[] b = new byte[3];
    char[] c = new char[3];
    short[] s = new short[3];
    int[] i = new int[3];
    long[] l = new long[3];
    float[] f = new float[3];
    double[] d = new double[3];
    Object[] o = new Object[3];
    p("defaults " + z[0] + " " + b[0] + " " + (int) c[0] + " " + s[0] + " " + i[0] + " " + l[0] + " " + f[0] + " " + d[0] + " " + o[0]);
    z[1] = true; b[1] = (byte) 0xff; c[1] = (char) 0xffff; s[1] = (short) 0x8000; i[1] = -1; l[1] = Long.MIN_VALUE; f[1] = -1.5f; d[1] = 1e300;
    o[1] = "x";
    p("stores " + z[1] + " " + b[1] + " " + (int) c[1] + " " + s[1] + " " + i[1] + " " + l[1] + " " + f[1] + " " + d[1] + " " + o[1]);
    l[2] = l[1] + 1;
    d[2] = d[1] * 2;
    b[2] = (byte) (b[1] + 1);
    c[2] = (char) (c[1] + 2);
    p("arith " + l[2] + " " + d[2] + " " + b[2] + " " + (int) c[2] + " " + (b[1] & 0xff) + " " + (s[1] >>> 16));
    // initializers and lengths
    int[][] tri = {{1}, {2, 3}, {4, 5, 6}};
    p("tri " + tri.length + " " + tri[2].length + " " + tri[2][1] + " " + str(tri[1]));
    // multianewarray
    int[][][] cube = new int[2][3][4];
    cube[1][2][3] = 7;
    long[][] partial = new long[3][];
    partial[1] = new long[5];
    String[][] names = new String[2][2];
    names[1][1] = "n";
    p("multi " + cube.length + cube[0].length + cube[0][0].length + " " + cube[1][2][3] + " " + partial[0] + " " + partial[1].length + " " + names[1][1] + " " + names[0][0]);
    check(cube[0][1] != cube[1][1], "subarrays distinct");
    int[][] zeroDim = new int[0][5];
    p("zero " + zeroDim.length);
    // arraycopy, overlapping both ways
    int[] a = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    System.arraycopy(a, 0, a, 2, 6);
    p("overlap+ " + str(a));
    int[] a2 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    System.arraycopy(a2, 3, a2, 1, 6);
    p("overlap- " + str(a2));
    Object[] objs = {"a", "b", "c"};
    String[] strs = new String[3];
    System.arraycopy(objs, 0, strs, 0, 3);
    p("objcopy " + strs[0] + strs[1] + strs[2]);
    Object[] mixed = {"a", new Integer(1), "c"};
    try { System.arraycopy(mixed, 0, strs, 0, 3); check(false, "arraycopy store check"); } catch (ArrayStoreException e) { p("ase copy " + strs[0]); }
    try { System.arraycopy(a, 0, new long[10], 0, 1); check(false, "int[] -> long[]"); } catch (ArrayStoreException e) { p("ase prim ok"); }
    try { System.arraycopy(a, 8, a, 0, 5); check(false, "arraycopy bounds"); } catch (ArrayIndexOutOfBoundsException e) { p("aioobe copy ok"); }
    try { System.arraycopy(a, -1, a, 0, 1); check(false, "arraycopy negative"); } catch (ArrayIndexOutOfBoundsException e) { p("aioobe neg ok"); }
    try { System.arraycopy(null, 0, a, 0, 1); check(false, "arraycopy null"); } catch (NullPointerException e) { p("npe copy ok"); }
    System.arraycopy(a, 0, a, 0, 0);
    char[] cc = "hello world".toCharArray();
    System.arraycopy(cc, 6, cc, 0, 5);
    p("chars " + new String(cc));
    // clone
    int[] cl = (int[]) a.clone();
    cl[0] = 99;
    String[][] deep = {{"p"}, {"q"}};
    String[][] shallow = (String[][]) deep.clone();
    p("clone " + a[0] + " " + cl[0] + " " + cl.length + " " + (shallow[1] == deep[1]) + " " + (shallow != deep));
    // covariance and store checks
    Object[] covariant = new String[2];
    covariant[0] = "ok";
    try { covariant[1] = new Integer(3); check(false, "ArrayStoreException"); } catch (ArrayStoreException e) { p("ase ok"); }
    p("types " + (covariant instanceof String[]) + " " + (covariant instanceof Object[]) + " " + ((Object) a instanceof int[]) + " " + ((Object) a instanceof Object) + " " + ((Object) tri instanceof Object[]) + " " + ((Object) tri instanceof int[][]));
    try { Object x = a; long[] bad = (long[]) x; check(false, "cast int[] to long[] " + bad.length); } catch (ClassCastException e) { p("cce ok"); }
    // exceptions
    try { i[id(3)] = 1; check(false, "store past end"); } catch (ArrayIndexOutOfBoundsException e) { p("aioobe ok"); }
    try { p("" + i[id(-1)]); check(false, "load at -1"); } catch (ArrayIndexOutOfBoundsException e) { p("aioobe ok"); }
    try { int[] n = new int[id(-2)]; check(false, "negative size " + n.length); } catch (NegativeArraySizeException e) { p("nase ok"); }
    try { int[][] n = new int[2][id(-2)]; check(false, "negative size 2 " + n.length); } catch (NegativeArraySizeException e) { p("nase2 ok"); }
    try { int[] n = null; p("" + n.length); check(false, "null length"); } catch (NullPointerException e) { p("npe len ok"); }
    try { int[] n = null; n[0] = 1; check(false, "null store"); } catch (NullPointerException e) { p("npe store ok"); }
    // class names
    p("names " + a.getClass().getName() + " " + tri.getClass().getName() + " " + strs.getClass().getName() + " " + new boolean[0].getClass().getName() + " " + new double[1][1].getClass().getName());
    // a bigger workload: bubble sort and checksum
    int[] big = new int[300];
    for (int k = 0; k < big.length; k++) big[k] = (k * 7919) % 1009;
    for (int x = 0; x < big.length; x++)
      for (int y = 0; y + 1 < big.length - x; y++)
        if (big[y] > big[y + 1]) { int t = big[y]; big[y] = big[y + 1]; big[y + 1] = t; }
    long sum = 0;
    for (int k = 0; k < big.length; k++) { sum = sum * 31 + big[k]; check(k == 0 || big[k - 1] <= big[k], "sorted"); }
    p("sorted " + big[0] + " " + big[299] + " " + sum);
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
