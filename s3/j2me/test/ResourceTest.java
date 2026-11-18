import java.io.*;

// Resources and classes from JARs (run.sh builds res.jar, deflated, and stored.jar, not compressed):
// getResourceAsStream absolute and relative to a class's package, Class.forName of a class inside a JAR, a 100 KB
// deflated text (dynamic Huffman blocks), a stored entry, UTF-8 text through InputStreamReader.
public class ResourceTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }

  static byte[] readAll(InputStream in) throws IOException {
    ByteArrayOutputStream o = new ByteArrayOutputStream();
    byte[] b = new byte[1000];
    int n;
    while ((n = in.read(b, 0, b.length)) > 0) o.write(b, 0, n);
    in.close();
    return o.toByteArray();
  }

  public static void main(String[] args) throws Exception {
    InputStream in = ResourceTest.class.getResourceAsStream("/res/hello.txt");
    check(in != null, "hello.txt");
    String hello = new String(readAll(in));
    check(hello.equals("Hello from a JAR\n"), "hello text [" + hello + "]");
    Class c = Class.forName("pkg.FromJar");
    Object o = c.newInstance();
    check(o.toString().equals("FromJar"), "class from the JAR " + o);
    // relative to pkg/
    byte[] data = readAll(c.getResourceAsStream("data.bin"));
    boolean ok = data.length == 10240;
    for (int i = 0; ok && i < data.length; i++) ok = (data[i] & 255) == i % 256;
    check(ok, "data.bin " + data.length);
    byte[] big = readAll(c.getResourceAsStream("/big.txt"));
    String text = new String(big);
    int lines = 0, from = 0;
    while (true) {
      int nl = text.indexOf('\n', from);
      if (nl < 0) break;
      if (!text.substring(from, nl).equals("line " + lines)) { check(false, "big.txt line " + lines); break; }
      lines++;
      from = nl + 1;
    }
    check(lines == 10000, "big.txt lines " + lines + " of " + big.length + " bytes");
    byte[] plain = readAll(ResourceTest.class.getResourceAsStream("/stored/plain.txt"));
    check(new String(plain).equals("stored entry\n"), "stored entry [" + new String(plain) + "]");
    check(ResourceTest.class.getResourceAsStream("/no/such.file") == null, "missing resource");
    Reader r = new InputStreamReader(ResourceTest.class.getResourceAsStream("/res/utf8.txt"), "UTF-8");
    StringBuffer sb = new StringBuffer();
    int ch;
    while ((ch = r.read()) >= 0) sb.append((char) ch);
    check(sb.toString().equals("Tiếng Việt có dấu\n"), "utf8.txt [" + sb + "]");
    DataInputStream din = new DataInputStream(ResourceTest.class.getResourceAsStream("/res/hello.txt"));
    byte[] five = new byte[5];
    din.readFully(five);
    check(new String(five).equals("Hello"), "readFully");
    p("resources: " + hello.length() + " + " + data.length + " + " + big.length + " + " + plain.length + " bytes");
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
