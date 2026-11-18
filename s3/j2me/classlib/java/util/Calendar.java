package java.util;

// CLDC's Calendar (Gregorian). set() recomputes at once: the fields stay normalized (set(MONTH, 13) is next year's
// February); HOUR (with AM_PM) or HOUR_OF_DAY counts, whichever was set last.
public abstract class Calendar {
  public static final int ERA = 0, YEAR = 1, MONTH = 2, DATE = 5, DAY_OF_MONTH = 5, DAY_OF_YEAR = 6, DAY_OF_WEEK = 7,
      AM_PM = 9, HOUR = 10, HOUR_OF_DAY = 11, MINUTE = 12, SECOND = 13, MILLISECOND = 14;
  public static final int SUNDAY = 1, MONDAY = 2, TUESDAY = 3, WEDNESDAY = 4, THURSDAY = 5, FRIDAY = 6, SATURDAY = 7;
  public static final int JANUARY = 0, FEBRUARY = 1, MARCH = 2, APRIL = 3, MAY = 4, JUNE = 5, JULY = 6, AUGUST = 7,
      SEPTEMBER = 8, OCTOBER = 9, NOVEMBER = 10, DECEMBER = 11;
  public static final int AM = 0, PM = 1;
  protected int[] fields = new int[17];
  protected boolean[] isSet = new boolean[17];
  protected long time;
  private TimeZone zone;
  boolean hour12;  // HOUR was set after HOUR_OF_DAY

  protected Calendar() { zone = TimeZone.getDefault(); }
  public static synchronized Calendar getInstance() {
    Calendar c = new Gregorian();
    c.setTimeInMillis(System.currentTimeMillis());
    return c;
  }
  public static synchronized Calendar getInstance(TimeZone z) {
    Calendar c = new Gregorian();
    c.zone = z;
    c.setTimeInMillis(System.currentTimeMillis());
    return c;
  }
  public final Date getTime() { return new Date(getTimeInMillis()); }
  public final void setTime(Date d) { setTimeInMillis(d.getTime()); }
  protected long getTimeInMillis() { return time; }
  protected void setTimeInMillis(long ms) { time = ms; computeFields(); }
  public final int get(int f) { return fields[f]; }
  public final void set(int f, int v) {
    fields[f] = v;
    isSet[f] = true;
    if (f == HOUR || f == AM_PM) hour12 = true;
    if (f == HOUR_OF_DAY) hour12 = false;
    computeTime();
    computeFields();
  }
  public TimeZone getTimeZone() { return zone; }
  public void setTimeZone(TimeZone z) { zone = z; computeFields(); }
  public boolean equals(Object o) { return o instanceof Calendar && ((Calendar) o).getTimeInMillis() == getTimeInMillis(); }
  public int hashCode() { long t = getTimeInMillis(); return (int) (t ^ (t >>> 32)); }
  public boolean before(Object o) { return o instanceof Calendar && getTimeInMillis() < ((Calendar) o).getTimeInMillis(); }
  public boolean after(Object o) { return o instanceof Calendar && getTimeInMillis() > ((Calendar) o).getTimeInMillis(); }
  protected abstract void computeFields();
  protected abstract void computeTime();

  static long floorDiv(long a, long b) { long q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
  // days since 1970-01-01 of a proleptic Gregorian date (month 1-12)
  static long daysFromCivil(long y, long m, long d) {
    y -= m <= 2 ? 1 : 0;
    long era = floorDiv(y, 400);
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
  }

  static final class Gregorian extends Calendar {
    protected void computeFields() {
      long local = time + getTimeZone().getRawOffset();
      long days = floorDiv(local, 86400000L);
      long ms = local - days * 86400000L;
      long z = days + 719468;
      long era = floorDiv(z, 146097);
      long doe = z - era * 146097;
      long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      long y = yoe + era * 400;
      long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      long mp = (5 * doy + 2) / 153;
      long d = doy - (153 * mp + 2) / 5 + 1;
      long m = mp < 10 ? mp + 3 : mp - 9;
      if (m <= 2) y++;
      fields[ERA] = y > 0 ? 1 : 0;
      fields[YEAR] = (int) y;
      fields[MONTH] = (int) m - 1;
      fields[DAY_OF_MONTH] = (int) d;
      fields[DAY_OF_YEAR] = (int) (days - daysFromCivil(y, 1, 1)) + 1;
      fields[DAY_OF_WEEK] = (int) (((days % 7) + 7 + 4) % 7) + 1;  // 1970-01-01 was a Thursday
      int msd = (int) ms;
      fields[HOUR_OF_DAY] = msd / 3600000;
      fields[AM_PM] = fields[HOUR_OF_DAY] >= 12 ? PM : AM;
      fields[HOUR] = fields[HOUR_OF_DAY] % 12;
      fields[MINUTE] = msd / 60000 % 60;
      fields[SECOND] = msd / 1000 % 60;
      fields[MILLISECOND] = msd % 1000;
    }
    protected void computeTime() {
      long y = fields[YEAR], mo = fields[MONTH];
      y += floorDiv(mo, 12);
      mo -= floorDiv(mo, 12) * 12;
      long days = daysFromCivil(y, mo + 1, 1) + fields[DAY_OF_MONTH] - 1;
      long h = hour12 ? fields[HOUR] + 12L * fields[AM_PM] : fields[HOUR_OF_DAY];
      long ms = ((h * 60 + fields[MINUTE]) * 60 + fields[SECOND]) * 1000 + fields[MILLISECOND];
      time = days * 86400000L + ms - getTimeZone().getRawOffset();
    }
  }
}
