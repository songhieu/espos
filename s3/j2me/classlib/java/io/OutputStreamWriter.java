package java.io;

// Chars to bytes: UTF-8 when asked for, else ISO-8859-1 (characters past 255 become '?').
public class OutputStreamWriter extends Writer {
  private OutputStream out;
  private final boolean utf8;
  public OutputStreamWriter(OutputStream os) {
    super(os);
    out = os;
    utf8 = false;
  }
  public OutputStreamWriter(OutputStream os, String enc) throws UnsupportedEncodingException {
    super(os);
    if (enc == null) throw new NullPointerException();
    out = os;
    String e = enc.toLowerCase();
    if (e.equals("utf-8") || e.equals("utf8")) utf8 = true;
    else if (e.equals("iso-8859-1") || e.equals("iso8859_1") || e.equals("us-ascii") || e.equals("ascii") || e.equals("latin1")) utf8 = false;
    else throw new UnsupportedEncodingException(enc);
  }
  public void write(int c) throws IOException {
    if (out == null) throw new IOException("Stream closed");
    c &= 0xffff;
    if (!utf8) { out.write(c < 256 ? c : '?'); return; }
    if (c < 0x80) out.write(c);
    else if (c < 0x800) { out.write(0xc0 | (c >> 6)); out.write(0x80 | (c & 0x3f)); }
    else { out.write(0xe0 | (c >> 12)); out.write(0x80 | ((c >> 6) & 0x3f)); out.write(0x80 | (c & 0x3f)); }
  }
  public void write(char[] c, int off, int len) throws IOException {
    if (off < 0 || len < 0 || off + len > c.length) throw new IndexOutOfBoundsException();
    for (int i = 0; i < len; i++) write(c[off + i]);
  }
  public void write(String s, int off, int len) throws IOException {
    for (int i = 0; i < len; i++) write(s.charAt(off + i));
  }
  public void flush() throws IOException { if (out != null) out.flush(); }
  public void close() throws IOException { if (out != null) { out.close(); out = null; } }
}
