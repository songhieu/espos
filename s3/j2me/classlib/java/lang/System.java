package java.lang;

public final class System {
  public static final java.io.PrintStream out = new java.io.PrintStream(1), err = new java.io.PrintStream(2);
  private System() {}
  public static native long currentTimeMillis();
  public static native void arraycopy(Object src, int sp, Object dst, int dp, int n);
  public static native int identityHashCode(Object o);
  public static native String getProperty(String key);
  public static void exit(int code) { Runtime.getRuntime().exit(code); }
  public static void gc() { Runtime.getRuntime().gc(); }
}
