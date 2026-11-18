package java.io;

// Bytes to chars: UTF-8 when asked for, else ISO-8859-1 (the VM's default encoding).
public class InputStreamReader extends Reader {
  private InputStream in;
  private final boolean utf8;
  private int pendingLow = -1;  // the second half of a surrogate pair
  public InputStreamReader(InputStream is) {
    super(is);
    in = is;
    utf8 = false;
  }
  public InputStreamReader(InputStream is, String enc) throws UnsupportedEncodingException {
    super(is);
    if (enc == null) throw new NullPointerException();
    in = is;
    String e = enc.toLowerCase();
    if (e.equals("utf-8") || e.equals("utf8")) utf8 = true;
    else if (e.equals("iso-8859-1") || e.equals("iso8859_1") || e.equals("us-ascii") || e.equals("ascii") || e.equals("latin1")) utf8 = false;
    else throw new UnsupportedEncodingException(enc);
  }
  public int read() throws IOException {
    if (in == null) throw new IOException("Stream closed");
    if (pendingLow >= 0) { int c = pendingLow; pendingLow = -1; return c; }
    int b = in.read();
    if (b < 0 || !utf8 || b < 0x80) return b;
    int n, cp;
    if ((b & 0xe0) == 0xc0) { n = 1; cp = b & 0x1f; }
    else if ((b & 0xf0) == 0xe0) { n = 2; cp = b & 0x0f; }
    else if ((b & 0xf8) == 0xf0) { n = 3; cp = b & 0x07; }
    else return 0xfffd;
    for (int i = 0; i < n; i++) {
      int c = in.read();
      if (c < 0) return 0xfffd;
      cp = (cp << 6) | (c & 0x3f);
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      pendingLow = 0xdc00 + (cp & 0x3ff);
      return 0xd800 + (cp >> 10);
    }
    return cp;
  }
  public int read(char[] c, int off, int len) throws IOException {
    if (off < 0 || len < 0 || off + len > c.length) throw new IndexOutOfBoundsException();
    if (len == 0) return 0;
    int i = 0;
    for (; i < len; i++) {
      if (i > 0 && pendingLow < 0 && in.available() <= 0) break;
      int ch = read();
      if (ch < 0) break;
      c[off + i] = (char) ch;
    }
    return i == 0 ? -1 : i;
  }
  public long skip(long n) throws IOException {
    long k = 0;
    while (k < n && read() >= 0) k++;
    return k;
  }
  public boolean ready() throws IOException { return pendingLow >= 0 || (in != null && in.available() > 0); }
  public void close() throws IOException { if (in != null) { in.close(); in = null; } }
}
