import java.io.*;
import java.util.*;

// java.util and java.io of the class library: Vector, Stack, Hashtable, Enumeration, Random (same sequence as the
// JDK), Calendar/Date/TimeZone, Data streams, readers/writers. Output compared with the JDK.
public class CollectionsTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }

  public static void main(String[] args) throws Exception {
    Vector v = new Vector(2);
    for (int i = 0; i < 10; i++) v.addElement(new Integer(i));
    v.insertElementAt("x", 3);
    v.removeElementAt(0);
    v.removeElement(new Integer(5));
    v.setElementAt("y", 0);
    p("vector " + v + " " + v.size() + " " + v.indexOf("x") + " " + v.contains(new Integer(9)) + " " + v.lastIndexOf(new Integer(9)) + " " + v.firstElement() + " " + v.lastElement());
    Object[] arr = new Object[v.size()];
    v.copyInto(arr);
    int sum = 0;
    for (Enumeration e = v.elements(); e.hasMoreElements();) { Object o = e.nextElement(); if (o instanceof Integer) sum += ((Integer) o).intValue(); }
    p("enum " + sum + " " + arr.length + " " + arr[2]);
    try { v.elementAt(100); check(false, "elementAt"); } catch (ArrayIndexOutOfBoundsException e) { p("aioobe ok"); }
    v.removeAllElements();
    p("empty " + v.isEmpty() + " " + v.size());
    Stack st = new Stack();
    st.push("a"); st.push("b"); st.push("c");
    p("stack " + st.pop() + st.peek() + st.size() + " " + st.search("a") + " " + st.empty());
    try { new Stack().pop(); check(false, "pop empty"); } catch (EmptyStackException e) { p("ese ok"); }
    // Hashtable: many keys, rehashing, remove, enumerate
    Hashtable h = new Hashtable();
    for (int i = 0; i < 2000; i++) h.put("key" + i, new Integer(i));
    for (int i = 0; i < 2000; i += 2) h.remove("key" + i);
    long hs = 0;
    for (Enumeration e = h.keys(); e.hasMoreElements();) hs += ((Integer) h.get(e.nextElement())).intValue();
    p("hashtable " + h.size() + " " + hs + " " + h.get("key7") + " " + h.get("key8") + " " + h.containsKey("key9") + " " + h.contains(new Integer(11)));
    Object old = h.put("key7", "seven");
    p("replace " + old + " " + h.get("key7") + " " + h.size());
    try { h.put("k", null); check(false, "null value"); } catch (NullPointerException e) { p("npe ok"); }
    Hashtable small = new Hashtable();
    small.put(new Integer(1), "one");
    p("small " + small + " " + small.isEmpty());
    // Random: the JDK's generator, so the same seed gives the same numbers
    Random r = new Random(42);
    StringBuffer rs = new StringBuffer();
    rs.append(r.nextInt()).append(' ').append(r.nextInt(100)).append(' ').append(r.nextInt(64)).append(' ').append(r.nextLong()).append(' ').append(r.nextFloat()).append(' ').append(r.nextDouble());
    p("random " + rs);
    int[] hist = new int[10];
    for (int i = 0; i < 10000; i++) hist[r.nextInt(10)]++;
    p("hist " + hist[0] + " " + hist[9]);
    // Calendar and Date in GMT
    TimeZone gmt = TimeZone.getTimeZone("GMT");
    Calendar cal = Calendar.getInstance(gmt);
    cal.setTime(new Date(1700000000000L));  // 2023-11-14 22:13:20 GMT, a Tuesday
    p("cal " + cal.get(Calendar.YEAR) + " " + cal.get(Calendar.MONTH) + " " + cal.get(Calendar.DAY_OF_MONTH) + " " + cal.get(Calendar.HOUR_OF_DAY) + " " + cal.get(Calendar.MINUTE) + " " + cal.get(Calendar.SECOND) + " " + cal.get(Calendar.DAY_OF_WEEK) + " " + cal.get(Calendar.AM_PM) + " " + cal.get(Calendar.HOUR));
    cal.set(Calendar.MONTH, Calendar.FEBRUARY);
    cal.set(Calendar.DAY_OF_MONTH, 29);
    p("leap " + cal.get(Calendar.MONTH) + " " + cal.get(Calendar.DAY_OF_MONTH) + " " + cal.getTime().getTime());
    cal.setTime(new Date(0));
    p("epoch " + cal.get(Calendar.YEAR) + " " + cal.get(Calendar.DAY_OF_WEEK) + " " + new Date(0).equals(cal.getTime()));
    cal.setTime(new Date(-86400000L * 365));
    p("before " + cal.get(Calendar.YEAR) + " " + cal.get(Calendar.MONTH) + " " + cal.get(Calendar.DAY_OF_MONTH));
    cal.setTime(new Date(4102444800000L));  // 2100-01-01
    p("y2100 " + cal.get(Calendar.YEAR) + " " + cal.get(Calendar.DAY_OF_WEEK));
    TimeZone plus7 = TimeZone.getTimeZone("GMT+07:00");
    p("tz " + plus7.getRawOffset() + " " + gmt.getRawOffset() + " " + plus7.useDaylightTime());
    // Data streams
    ByteArrayOutputStream bo = new ByteArrayOutputStream();
    DataOutputStream out = new DataOutputStream(bo);
    out.writeBoolean(true); out.writeByte(-2); out.writeShort(-3); out.writeChar('Z'); out.writeInt(-4); out.writeLong(-5L);
    out.writeFloat(6.5f); out.writeDouble(-7.25); out.writeUTF("utf é中"); out.writeChars("ab");
    byte[] data = bo.toByteArray();
    p("dataout " + data.length + " " + out.size());
    DataInputStream in = new DataInputStream(new ByteArrayInputStream(data));
    p("datain " + in.readBoolean() + " " + in.readByte() + " " + in.readShort() + " " + in.readChar() + " " + in.readInt() + " " + in.readLong() + " " + in.readFloat() + " " + in.readDouble() + " " + in.readUTF() + " " + in.readChar() + in.readChar() + " " + in.available());
    try { in.readInt(); check(false, "EOF"); } catch (EOFException e) { p("eof ok"); }
    ByteArrayInputStream bi = new ByteArrayInputStream(new byte[] {1, 2, 3, 4, 5});
    bi.skip(1);
    bi.mark(10);
    byte[] two = new byte[2];
    p("bais " + bi.read() + " " + bi.read(two, 0, 2) + " " + two[1] + " " + bi.available());
    bi.reset();
    p("reset " + bi.read() + " " + bi.markSupported());
    // readers and writers
    ByteArrayOutputStream wb = new ByteArrayOutputStream();
    Writer wr = new OutputStreamWriter(wb, "UTF-8");
    wr.write("Xin chào 世界");
    wr.write('!');
    wr.flush();
    byte[] wbytes = wb.toByteArray();
    Reader rd = new InputStreamReader(new ByteArrayInputStream(wbytes), "UTF-8");
    StringBuffer text = new StringBuffer();
    int ch;
    while ((ch = rd.read()) >= 0) text.append((char) ch);
    p("reader " + wbytes.length + " " + text + " " + text.length());
    char[] cbuf = new char[64];
    int n = new InputStreamReader(new ByteArrayInputStream(new byte[] {65, 66, 67})).read(cbuf, 0, 64);
    p("latin " + n + " " + new String(cbuf, 0, n));
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
