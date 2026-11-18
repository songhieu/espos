// jsr/ret finally blocks as Java 1.1-1.4 compilers made them (Jsr.class from gen_jsr.py, class file version 45.3).
// Output compared with the JDK (which still runs version 45 classes).
public class JsrTest {
  public static void main(String[] args) {
    int fails = 0;
    StringBuffer out = new StringBuffer();
    out.append(Jsr.f(5)).append(' ').append(Jsr.counter).append(' ');
    try { Jsr.f(0); fails++; } catch (ArithmeticException e) { out.append("ae "); }
    out.append(Jsr.counter).append(' ');
    out.append(Jsr.g(2)).append(' ').append(Jsr.counter).append(' ');
    try { Jsr.g(0); fails++; } catch (ArithmeticException e) { out.append("ae "); }
    out.append(Jsr.counter);
    int sum = 0;
    for (int i = 1; i <= 1000; i++) sum += Jsr.f(i) + Jsr.g(i);
    out.append(' ').append(sum).append(' ').append(Jsr.counter);
    System.out.println(out);
    if (Jsr.counter != 2004 || !out.toString().startsWith("2 1 ae 2 5 3 ae 4")) fails++;
    System.out.println(fails == 0 ? "ALL OK" : fails + " FAILED");
  }
}
