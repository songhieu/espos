// Objects: inheritance, overriding, super calls, interfaces (incl. through abstract classes), static/instance init
// order, inner/anonymous/local classes, fields of every type, Class reflection (CLDC subset). Output compared with
// the JDK.
public class ObjectTest {
  static int fails;
  static void check(boolean ok, String what) { if (!ok) { fails++; System.out.println("FAIL " + what); } }
  static void p(String s) { System.out.println(s); }
  static StringBuffer order = new StringBuffer();

  interface Named { String name(); }
  interface Greeter extends Named { int GREETING_LEN = 5; String greet(); }
  interface Counter { int count(); }
  static abstract class Base implements Greeter {
    static { order.append("Base.static "); }
    protected int id;
    { order.append("Base.init "); }
    Base(int id) { this.id = id; order.append("Base() "); hook(); }
    void hook() { order.append("Base.hook "); }
    public String greet() { return "Hello " + name(); }
    public abstract String name();
    String who() { return "base"; }
    static String stat() { return "Base.stat"; }
  }
  static class Child extends Base implements Counter {
    static { order.append("Child.static "); }
    String nm = "child";
    { order.append("Child.init "); }
    Child(int id) { super(id); order.append("Child() "); }
    void hook() { order.append("Child.hook(nm=" + nm + ") "); }
    public String name() { return nm + id; }
    public int count() { return id * 10; }
    String who() { return "child>" + super.who(); }
    static String stat() { return "Child.stat"; }
  }
  static class GrandChild extends Child {
    GrandChild() { super(3); }
    public String name() { return "grand" + super.name(); }
    String who() { return "grand>" + super.who(); }
  }
  // a class that gets an interface method from its superclass
  static class HasRun { public void run() { order.append("HasRun.run "); } }
  static class Runs extends HasRun implements Runnable {}
  // an abstract class implementing an interface without declaring the method (invokevirtual through it)
  static abstract class AbstractTask implements Runnable { void go() { run(); } }
  static class Task extends AbstractTask { int n; public void run() { n++; } }

  static class Fields {
    boolean z = true; byte b = -1; char c = 'c'; short s = -2; int i = -3; long l = -4L; float f = -5.5f; double d = -6.5; Object o = "o";
    static boolean sz; static byte sb; static char sc; static short ss; static int si; static long sl; static float sf; static double sd; static Object so;
    static final int CONST_I = 42; static final long CONST_L = 1L << 40; static final String CONST_S = "const"; static final double CONST_D = 2.5;
  }

  class Inner { int v() { return outerField * 2; } }
  int outerField = 21;

  static class Lazy {
    static { order.append("Lazy.static "); }
    static int touch() { return 1; }
  }
  static class Point {
    final int x, y;
    Point(int x, int y) { this.x = x; this.y = y; }
    public boolean equals(Object o) { return o instanceof Point && ((Point) o).x == x && ((Point) o).y == y; }
    public int hashCode() { return x * 31 + y; }
    public String toString() { return "(" + x + "," + y + ")"; }
  }

  public static void main(String[] args) throws Exception {
    Child c = new Child(1);
    p("order " + order);
    order.setLength(0);
    Greeter g = c;
    Named n = c;
    Counter k = c;
    p("calls " + g.greet() + " " + n.name() + " " + k.count() + " " + c.who() + " " + Base.stat() + " " + Child.stat() + " " + Greeter.GREETING_LEN);
    GrandChild gc = new GrandChild();
    order.setLength(0);
    Base asBase = gc;
    p("grand " + asBase.greet() + " " + asBase.who() + " " + ((Counter) asBase).count());
    p("instanceof " + (gc instanceof Base) + " " + (gc instanceof Named) + " " + (gc instanceof Counter) + " " + ((Object) c instanceof GrandChild) + " " + (null instanceof Object));
    Runnable r = new Runs();
    r.run();
    Task t = new Task();
    t.go();
    t.go();
    ((AbstractTask) t).run();
    p("iface via super " + order + t.n);
    // anonymous and local classes capturing variables
    final int captured = 5;
    Named anon = new Named() { public String name() { return "anon" + captured; } };
    class Local implements Named { public String name() { return "local" + captured * 2; } }
    p("inner " + anon.name() + " " + new Local().name() + " " + new ObjectTest().new Inner().v());
    // fields
    Fields f = new Fields();
    p("fields " + f.z + f.b + f.c + f.s + f.i + f.l + f.f + f.d + f.o);
    p("statics " + Fields.sz + Fields.sb + (int) Fields.sc + Fields.ss + Fields.si + Fields.sl + Fields.sf + Fields.sd + Fields.so);
    Fields.sl = Long.MAX_VALUE; Fields.sd = 1.25; Fields.si = 9; Fields.so = f;
    f.l += 10; f.d *= 2; f.i <<= 3;
    p("set " + Fields.sl + " " + Fields.sd + " " + Fields.si + " " + (Fields.so == f) + " " + f.l + " " + f.d + " " + f.i);
    p("consts " + Fields.CONST_I + " " + Fields.CONST_L + " " + Fields.CONST_S + " " + Fields.CONST_D);
    // lazy class initialization: not before first use
    order.setLength(0);
    order.append("before ");
    Lazy.touch();
    p("lazy " + order);
    // equals/hashCode/toString overrides used by the class library
    java.util.Hashtable h = new java.util.Hashtable();
    h.put(new Point(1, 2), "a");
    p("point " + h.get(new Point(1, 2)) + " " + new Point(3, 4) + " " + new Point(1, 1).equals(new Point(1, 1)));
    // Class
    Class cls = c.getClass();
    p("class " + cls.getName() + " " + cls.isInstance(gc) + " " + Base.class.isAssignableFrom(GrandChild.class) + " " + Named.class.isInterface() + " " + cls.isArray() + " " + (cls == Child.class) + " " + String.class);
    Class pc = Class.forName("ObjectTest$Point");
    p("forName " + pc.getName() + " " + (pc == Point.class));
    Object made = Class.forName("ObjectTest$Made").newInstance();
    p("newInstance " + made);
    try { Class.forName("ObjectTest$Base").newInstance(); check(false, "abstract newInstance"); } catch (InstantiationException e) { p("ie ok"); }
    p("hash " + (c.hashCode() == System.identityHashCode(c)) + " " + System.identityHashCode(null) + " " + (c.equals(c)) + " " + c.equals(gc));
    p(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
  public static class Made {
    public Made() {}
    public String toString() { return "made"; }
  }
}
