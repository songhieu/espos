package java.util;

public class Date {
  private long time;
  public Date() { this(System.currentTimeMillis()); }
  public Date(long t) { time = t; }
  public long getTime() { return time; }
  public void setTime(long t) { time = t; }
  public boolean equals(Object o) { return o instanceof Date && ((Date) o).time == time; }
  public int hashCode() { return (int) (time ^ (time >>> 32)); }
  // "Tue Nov 17 11:24:05 GMT 2026"
  public String toString() {
    Calendar c = Calendar.getInstance();
    c.setTime(this);
    String[] days = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    String[] months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    StringBuffer b = new StringBuffer();
    b.append(days[c.get(Calendar.DAY_OF_WEEK) - 1]).append(' ').append(months[c.get(Calendar.MONTH)]).append(' ');
    two(b, c.get(Calendar.DAY_OF_MONTH)).append(' ');
    two(b, c.get(Calendar.HOUR_OF_DAY)).append(':');
    two(b, c.get(Calendar.MINUTE)).append(':');
    two(b, c.get(Calendar.SECOND)).append(' ');
    b.append(c.getTimeZone().getID()).append(' ').append(c.get(Calendar.YEAR));
    return b.toString();
  }
  private static StringBuffer two(StringBuffer b, int v) { if (v < 10) b.append('0'); return b.append(v); }
}
