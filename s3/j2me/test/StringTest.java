// Strings, StringBuffer/StringBuilder, intern, encodings (src/j2me natives + classlib). Output compared with the JDK.
public class StringTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }
  static final String CONST = "hel" + "lo";

  static int kind(String s) {
    switch (s) {
      case "apple": return 1;
      case "banana": return 2;
      case "Aa": return 3;   // "Aa" and "BB" share a hash code
      case "BB": return 4;
      default: return 0;
    }
  }

  public static void main(String[] args) throws Exception {
    String s = "hello";
    p("hash " + s.hashCode() + " " + "".hashCode() + " " + "Hello, World".hashCode() + " " + "Aa".hashCode() + " " + "BB".hashCode());
    String built = new StringBuffer("hel").append("lo").toString();
    p("eq " + s.equals(built) + " " + (s == built) + " " + (s == CONST) + " " + (s == built.intern()) + " " + (built.intern() == "hello") + " " + s.equals(null));
    check(s == CONST && s == built.intern() && s != built, "intern");
    p("idx " + s.indexOf('l') + " " + s.lastIndexOf('l') + " " + s.indexOf("lo") + " " + s.indexOf("x") + " " + s.indexOf('l', 3) + " " + s.lastIndexOf("l") + " " + s.indexOf("") + " " + "aaa".lastIndexOf("aa"));
    p("sub " + s.substring(1) + " " + s.substring(1, 3) + " " + s.substring(5).length() + " [" + "  pad \t".trim() + "] " + "x".trim() + " " + s.charAt(4));
    p("case " + "MiXeD 123".toUpperCase() + " " + "MiXeD".toLowerCase() + " " + "abc".equalsIgnoreCase("ABC") + " " + "abc".compareTo("abd") + " " + "b".compareTo("abc") + " " + "ab".compareTo("abc"));
    p("misc " + s.startsWith("he") + " " + s.endsWith("lo") + " " + s.startsWith("lo", 3) + " " + s.replace('l', 'L') + " " + s.concat("!") + " " + "".length() + " " + "a,b".indexOf(','));
    p("region " + "Hello".regionMatches(true, 1, "XELLX", 1, 3) + " " + "Hello".regionMatches(1, "XELLX", 1, 3));
    try { s.charAt(5); check(false, "charAt(5)"); } catch (StringIndexOutOfBoundsException e) { p("sioobe ok"); }
    try { s.substring(3, 2); check(false, "substring(3,2)"); } catch (StringIndexOutOfBoundsException e) { p("sioobe2 ok"); }
    // valueOf of every type
    char[] cs = {'x', 'y', 'z'};
    p("valueOf " + String.valueOf(true) + " " + String.valueOf('c') + " " + String.valueOf(-12) + " " + String.valueOf(1L << 40) + " " + String.valueOf(1.5f) + " " + String.valueOf(-2.25) + " " + String.valueOf(cs) + " " + String.valueOf(cs, 1, 2) + " " + String.valueOf((Object) null));
    p("concat " + 1 + 2 + " " + (1 + 2) + " " + 'a' + 'b' + " " + ('a' + 'b') + " " + null + " " + 1.0 + " " + 1.0f + " " + (Object) "o" + " " + new char[] {'q'}.length);
    // StringBuffer
    StringBuffer sb = new StringBuffer();
    sb.append("abc").append(1).append('d').append(2L).append(true).append(1.5).append(new char[] {'e', 'f'}).append((Object) null);
    p("sb " + sb + " " + sb.length());
    sb.insert(0, "X").insert(2, 7).insert(sb.length(), '!');
    sb.setCharAt(1, 'A');
    sb.deleteCharAt(0);
    sb.delete(3, 5);
    p("sb2 " + sb + " " + sb.charAt(2));
    sb.setLength(3);
    p("sb3 " + sb + " " + sb.reverse() + " " + new StringBuffer("racecar").reverse());
    StringBuilder b2 = new StringBuilder(2);
    for (int i = 0; i < 100; i++) b2.append(i % 10);
    p("sb4 " + b2.length() + " " + b2.toString().substring(95));
    // hash codes of many strings (open addressing tables rely on these)
    int h = 0;
    for (int i = 0; i < 1000; i++) h = h * 31 + ("k" + i).hashCode();
    p("hashes " + h);
    // string switch (hashCode + equals + lookupswitch)
    p("switch " + kind("apple") + kind("banana") + kind("Aa") + kind("BB") + kind("cherry"));
    // bytes and encodings
    String viet = "Tiếng Việt có dấu €";
    byte[] u8 = viet.getBytes("UTF-8");
    p("utf8 " + u8.length + " " + (u8[2] & 0xff) + " " + new String(u8, "UTF-8").equals(viet) + " " + new String(u8, 0, 4, "UTF-8"));
    byte[] l1 = "café".getBytes("ISO-8859-1");
    p("latin1 " + l1.length + " " + (l1[3] & 0xff) + " " + new String(l1, "ISO-8859-1") + " " + new String(new byte[] {72, 105}));
    try { "x".getBytes("NOPE-1"); check(false, "bad encoding"); } catch (java.io.UnsupportedEncodingException e) { p("uee ok"); }
    String supp = "a😀b";
    byte[] su = supp.getBytes("UTF-8");
    p("supp " + supp.length() + " " + su.length + " " + new String(su, "UTF-8").equals(supp));
    // chars of a String
    char[] arr = "chars".toCharArray();
    arr[0] = 'C';
    char[] dst = new char[7];
    "xyzw".getChars(1, 3, dst, 2);
    p("chars " + new String(arr) + " " + new String(dst, 2, 2) + " " + new String(arr, 1, 3) + " " + String.valueOf(arr).length());
    // Integer/Long to String and back for many values
    int sum = 0;
    for (int i = -500; i <= 500; i += 7) sum += Integer.parseInt(Integer.toString(i * 12345));
    p("roundtrip " + sum + " " + Long.parseLong(Long.toString(Long.MIN_VALUE)) + " " + Integer.toString(Integer.MIN_VALUE));
    // Object.toString uses the class name and a hex hash; check the shape only
    String os = new Object().toString();
    p("objstr " + os.startsWith("java.lang.Object@"));
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
