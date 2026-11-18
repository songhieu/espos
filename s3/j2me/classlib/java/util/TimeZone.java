package java.util;

// CLDC's TimeZone: fixed offsets, no daylight saving. The default comes from the system properties
// "com.espos.tz.offset" (minutes east of GMT, e.g. 420 for Vietnam) and "com.espos.tz.id" (the VM host sets
// them), else from the device's local time (the C library's TZ; the offset in effect now).
public abstract class TimeZone {
  private static TimeZone deflt;
  public TimeZone() {}
  public abstract int getOffset(int era, int year, int month, int day, int dayOfWeek, int millis);
  public abstract int getRawOffset();
  public abstract boolean useDaylightTime();
  public String getID() { return "GMT"; }
  public static synchronized TimeZone getDefault() {
    if (deflt == null) {
      int minutes = hostOffset();
      String m = System.getProperty("com.espos.tz.offset");
      if (m != null) {
        try { minutes = Integer.parseInt(m.trim()); } catch (NumberFormatException e) {}
      }
      String id = System.getProperty("com.espos.tz.id");
      deflt = new Fixed(id != null ? id : name(minutes), minutes * 60000);
    }
    return deflt;
  }
  public static synchronized TimeZone getTimeZone(String id) {
    if (id == null) throw new NullPointerException();
    TimeZone d = getDefault();
    if (id.equals(d.getID())) return d;
    if ((id.startsWith("GMT") || id.startsWith("UTC")) && id.length() > 3) {
      // GMT+7, GMT+07:00, GMT-0530
      try {
        int sign = id.charAt(3) == '-' ? -1 : 1;
        String r = id.substring(4);
        int h, mi = 0, colon = r.indexOf(':');
        if (colon >= 0) { h = Integer.parseInt(r.substring(0, colon)); mi = Integer.parseInt(r.substring(colon + 1)); }
        else if (r.length() > 2) { h = Integer.parseInt(r.substring(0, r.length() - 2)); mi = Integer.parseInt(r.substring(r.length() - 2)); }
        else h = Integer.parseInt(r);
        int minutes = sign * (h * 60 + mi);
        return new Fixed(name(minutes), minutes * 60000);
      } catch (RuntimeException e) {}
    }
    return new Fixed(id.equals("UTC") ? "UTC" : "GMT", 0);
  }
  public static String[] getAvailableIDs() {
    String d = getDefault().getID();
    return d.equals("GMT") ? new String[] {"GMT", "UTC"} : new String[] {"GMT", "UTC", d};
  }
  private static native int hostOffset();
  private static String name(int minutes) {
    if (minutes == 0) return "GMT";
    int a = minutes < 0 ? -minutes : minutes;
    StringBuffer b = new StringBuffer("GMT").append(minutes < 0 ? '-' : '+');
    int h = a / 60, m = a % 60;
    if (h < 10) b.append('0');
    b.append(h).append(':');
    if (m < 10) b.append('0');
    return b.append(m).toString();
  }

  static final class Fixed extends TimeZone {
    private final String id;
    private final int offset;
    Fixed(String id, int offset) { this.id = id; this.offset = offset; }
    public int getOffset(int era, int year, int month, int day, int dayOfWeek, int millis) { return offset; }
    public int getRawOffset() { return offset; }
    public boolean useDaylightTime() { return false; }
    public String getID() { return id; }
  }
}
