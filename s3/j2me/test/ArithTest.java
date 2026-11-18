// Integer, long, float and double edge cases (src/j2me interpreter). Prints every value: run.sh compares the output
// with the JDK's for the same class file.
public class ArithTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }
  static int zero() { return 0; }
  static long lzero() { return 0L; }
  static int id(int x) { return x; }
  static double did(double x) { return x; }
  static float fid(float x) { return x; }

  public static void main(String[] args) {
    // int overflow and division
    int max = Integer.MAX_VALUE, min = Integer.MIN_VALUE;
    p("int " + (max + 1) + " " + (min - 1) + " " + (max * 2) + " " + (min * -1) + " " + (-min));
    p("div " + (min / id(-1)) + " " + (min % id(-1)) + " " + (-7 / 2) + " " + (-7 % 2) + " " + (7 % -2) + " " + (-7 / -2));
    check(min / id(-1) == min && min % id(-1) == 0, "MIN/-1");
    try { p("" + (5 / zero())); check(false, "int / 0"); } catch (ArithmeticException e) { p("int/0 " + e.getMessage()); }
    try { p("" + (5 % zero())); check(false, "int % 0"); } catch (ArithmeticException e) { p("int%0 ok"); }
    try { p("" + (5L / lzero())); check(false, "long / 0"); } catch (ArithmeticException e) { p("long/0 ok"); }
    try { p("" + (5L % lzero())); check(false, "long % 0"); } catch (ArithmeticException e) { p("long%0 ok"); }
    // shifts
    int s33 = id(33), s65 = id(65), sm1 = id(-1);
    p("shift " + (1 << s33) + " " + (-1 >>> 28) + " " + (-16 >> 2) + " " + (-16 >>> 28) + " " + (1 << sm1) + " " + (0x80000000 >> 31));
    p("lshift " + (1L << s65) + " " + (-1L >>> 60) + " " + (-16L >> 2) + " " + (1L << 63) + " " + ((1L << 63) >> 63) + " " + ((1L << 63) >>> 63));
    check((1 << s33) == 2 && (-1 >>> 28) == 15 && (1L << s65) == 2L, "shift counts");
    // long
    long lmax = Long.MAX_VALUE, lmin = Long.MIN_VALUE;
    p("long " + (lmax + 1) + " " + (lmin - 1) + " " + (lmin / -1L) + " " + (lmin % -1L) + " " + (lmax * 3) + " " + (-lmin));
    p("longs " + (123456789012L * 1000) + " " + (0xffffffffL + 1) + " " + ((long) id(-1) & 0xffffffffL) + " " + (long) min);
    long a = 0x123456789abcdefL;
    p("lbits " + (a & 0xff00ff00ff00L) + " " + (a | 1) + " " + (a ^ -1L) + " " + (~a) + " " + (a >> 13) + " " + (a >>> 40));
    p("lcmp " + (lmax > lmin) + " " + (lmin < 0) + " " + (lmax == Long.MAX_VALUE) + " " + Long.toString(lmin, 16) + " " + Long.toString(lmax, 36));
    // conversions
    p("i2x " + (byte) 200 + " " + (short) 70000 + " " + (char) 65 + " " + (int) (char) -1 + " " + (byte) -129 + " " + (int) 3.99 + " " + (int) -3.99);
    double nan = did(Double.NaN), inf = did(Double.POSITIVE_INFINITY);
    p("d2i " + (int) nan + " " + (int) inf + " " + (int) -inf + " " + (int) 1e20 + " " + (int) -1e20 + " " + (long) 1e30 + " " + (long) -1e30 + " " + (long) nan);
    float fnan = fid(Float.NaN), finf = fid(Float.POSITIVE_INFINITY);
    p("f2i " + (int) fnan + " " + (int) finf + " " + (long) -finf + " " + (int) 3e9f + " " + (long) 1e19f + " " + (int) -0.5f);
    p("l2f " + (float) lmax + " " + (double) lmax + " " + (float) 16777217 + " " + (double) (1L << 53) + " " + (float) 0.1 + " " + (double) 0.1f);
    check((int) nan == 0 && (int) inf == max && (long) -1e30 == lmin, "d2i saturation");
    // floats
    p("float " + (0.1f + 0.2f) + " " + (1.0f / 3) + " " + (fid(1) / 0) + " " + (fid(-1) / 0) + " " + (fid(0) / 0) + " " + (5.5f % 2) + " " + (-5.5f % 2));
    p("double " + (0.1 + 0.2) + " " + (1.0 / 3) + " " + (did(1) / 0) + " " + (did(0) / 0) + " " + (5.5 % 2) + " " + (-5.5 % 2) + " " + (1e300 * 1e10));
    p("nan " + (nan == nan) + " " + (nan != nan) + " " + (nan < 1) + " " + (nan > 1) + " " + (nan <= nan) + " " + (fnan == fnan) + " " + Double.isNaN(nan));
    p("zero " + (0.0 == -0.0) + " " + (1 / did(-0.0)) + " " + (1 / Math.min(did(-0.0), 0.0)) + " " + (1 / Math.max(did(-0.0), 0.0)) + " " + Math.abs(did(-0.0)));
    p("minmax " + Math.min(nan, 1) + " " + Math.max(1, nan) + " " + Math.min(fid(2), fid(-3)) + " " + Math.max(7L, -8L) + " " + Math.min(-0.0f, 0.0f));
    p("math " + Math.sqrt(2) + " " + Math.floor(-1.5) + " " + Math.ceil(-1.5) + " " + Math.abs(min) + " " + Math.abs(-2.5f) + " " + Math.sqrt(-1));
    p("trig " + Math.sin(0) + " " + Math.cos(0) + " " + (Math.abs(Math.sin(Math.PI / 6) - 0.5) < 1e-12) + " " + (Math.abs(Math.tan(Math.PI / 4) - 1) < 1e-12) + " " + Math.toDegrees(Math.PI));
    p("bits " + Integer.toHexString(Float.floatToIntBits(1.0f)) + " " + Long.toString(Double.doubleToLongBits(1.0), 16) + " " + Float.intBitsToFloat(0x40490fdb) + " " + Double.longBitsToDouble(0x400921fb54442d18L) + " " + Integer.toHexString(Float.floatToIntBits(fnan)));
    p("limits " + Float.MAX_VALUE + " " + Float.MIN_VALUE + " " + Double.MAX_VALUE + " " + Double.MIN_VALUE + " " + (Double.MAX_VALUE * 2) + " " + (float) 1e39);
    // comparisons the compiler turns into fcmpl/fcmpg/dcmpl/dcmpg
    int c = 0;
    for (int i = 0; i < 4; i++) {
      double x = i == 0 ? nan : i - 1.5, y = 0.5;
      if (x < y) c += 1;
      if (x > y) c += 10;
      if (x <= y) c += 100;
      if (x >= y) c += 1000;
      if (!(x < y)) c += 10000;
    }
    p("cmp " + c);
    // chars, bytes, compound assignment narrowing
    char ch = 'A';
    ch += 2;
    ch++;
    byte b = 120;
    b += 10;
    short sh = 32767;
    sh++;
    int ii = 5;
    ii *= 2.6;
    long ll = 10;
    ll /= 3.0;
    p("narrow " + ch + " " + (int) ch + " " + b + " " + sh + " " + ii + " " + ll + " " + (char) ('a' + 25) + " " + ('a' + 1));
    // iinc with big and negative constants (wide iinc)
    int k = 0;
    k += 1000;
    k -= 70000;
    k += 127;
    k -= 128;
    p("iinc " + k);
    // ternary chains, boolean logic, bit tricks
    int x = id(-37);
    p("misc " + (x < 0 ? -x : x) + " " + (x & 0xff) + " " + (x >>> 24) + " " + Integer.toBinaryString(5) + " " + Integer.toOctalString(8) + " " + Integer.toHexString(min) + " " + Integer.toString(255, 16) + " " + Integer.toString(-255, 2));
    p("parse " + Integer.parseInt("-2147483648") + " " + Integer.parseInt("7fffffff", 16) + " " + Long.parseLong("-9223372036854775808") + " " + Integer.parseInt("+42") + " " + Integer.valueOf("123").intValue());
    String[] bad = {"", "-", "12a", "2147483648", "0x10", " 1"};
    for (int i = 0; i < bad.length; i++) {
      try { Integer.parseInt(bad[i]); p("parsed? " + bad[i]); check(false, "parseInt(" + bad[i] + ")"); } catch (NumberFormatException e) { p("nfe " + i); }
    }
    p("dparse " + Double.parseDouble("1.5") + " " + Double.parseDouble("-2.5e3") + " " + Double.parseDouble("  .5  ") + " " + Float.parseFloat("3.25f") + " " + Double.parseDouble("1e-5") + " " + Double.parseDouble("NaN") + " " + Double.parseDouble("-Infinity"));
    try { Double.parseDouble("1.2.3"); check(false, "parseDouble 1.2.3"); } catch (NumberFormatException e) { p("dnfe ok"); }
    // printing doubles and floats the way Java does (shortest digits; not 1e23: JDK 17 prints 9.999999999999999E22,
    // a bug JDK 19 fixed, the VM prints 1.0E23)
    double[] ds = {0, -0.0, 1, -1, 0.1, 0.5, 100, 1234567, 1e7, 12345678.9, 0.001, 0.0001, 1e-5, 123.456, 1e21, 1e100, 3.0e-10, Math.PI, Math.E, 2.0 / 3, 1e22, 4.35, 0.3, 100.0 / 3};
    StringBuffer sb = new StringBuffer("dstr");
    for (int i = 0; i < ds.length; i++) sb.append(' ').append(ds[i]);
    p(sb.toString());
    float[] fs = {0, 1, 0.1f, 0.2f, 0.3f, 1.0f / 3, 100, 1e7f, 1.5e-4f, 3.4e38f, 16777216f, 2.5e-3f, 123.456f};
    sb = new StringBuffer("fstr");
    for (int i = 0; i < fs.length; i++) sb.append(' ').append(fs[i]);
    p(sb.toString());
    p("boxes " + new Integer(5).equals(new Integer(5)) + " " + new Long(5).hashCode() + " " + new Double(1.5).hashCode() + " " + new Float(1.5f).hashCode() + " " + new Character('x').charValue() + " " + new Boolean(true));
    p("char " + Character.isDigit('7') + " " + Character.isUpperCase('a') + " " + Character.toUpperCase('q') + " " + Character.digit('f', 16) + " " + Character.forDigit(11, 16) + " " + Character.isLowerCase('z'));
    // a loop with longs and doubles as locals (wide slots)
    long acc = 1;
    double dacc = 1;
    for (int i = 1; i <= 20; i++) { acc *= i; dacc *= i; }
    p("fact " + acc + " " + dacc);
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
