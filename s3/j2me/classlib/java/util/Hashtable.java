package java.util;

public class Hashtable {
  private static class Entry { final Object key; Object value; Entry next; final int hash;
    Entry(Object k, Object v, int h, Entry n) { key = k; value = v; hash = h; next = n; } }
  private Entry[] table;
  private int count;
  public Hashtable() { this(11); }
  public Hashtable(int cap) { table = new Entry[Math.max(cap, 1)]; }
  public int size() { return count; }
  public boolean isEmpty() { return count == 0; }
  private int slot(int h) { return (h & 0x7fffffff) % table.length; }
  public synchronized Object get(Object k) {
    int h = k.hashCode();
    for (Entry e = table[slot(h)]; e != null; e = e.next) if (e.hash == h && e.key.equals(k)) return e.value;
    return null;
  }
  public boolean containsKey(Object k) { return get(k) != null; }
  public synchronized boolean contains(Object v) {
    if (v == null) throw new NullPointerException();
    for (int i = 0; i < table.length; i++) for (Entry e = table[i]; e != null; e = e.next) if (e.value.equals(v)) return true;
    return false;
  }
  public synchronized Object put(Object k, Object v) {
    if (v == null) throw new NullPointerException();
    int h = k.hashCode();
    for (Entry e = table[slot(h)]; e != null; e = e.next) if (e.hash == h && e.key.equals(k)) { Object o = e.value; e.value = v; return o; }
    if (count >= table.length * 3 / 4) rehash();
    int s = slot(h);
    table[s] = new Entry(k, v, h, table[s]);
    count++;
    return null;
  }
  protected void rehash() {
    Entry[] old = table;
    table = new Entry[old.length * 2 + 1];
    for (int i = 0; i < old.length; i++)
      for (Entry e = old[i]; e != null;) { Entry n = e.next; int s = slot(e.hash); e.next = table[s]; table[s] = e; e = n; }
  }
  public synchronized Object remove(Object k) {
    int h = k.hashCode(), s = slot(h);
    for (Entry e = table[s], p = null; e != null; p = e, e = e.next)
      if (e.hash == h && e.key.equals(k)) { if (p == null) table[s] = e.next; else p.next = e.next; count--; return e.value; }
    return null;
  }
  public synchronized void clear() { for (int i = 0; i < table.length; i++) table[i] = null; count = 0; }
  private Enumeration walk(final boolean keys) {
    final Vector v = new Vector(count);
    for (int i = 0; i < table.length; i++) for (Entry e = table[i]; e != null; e = e.next) v.addElement(keys ? e.key : e.value);
    return v.elements();
  }
  public synchronized Enumeration keys() { return walk(true); }
  public synchronized Enumeration elements() { return walk(false); }
  public synchronized String toString() {
    StringBuffer b = new StringBuffer("{");
    boolean first = true;
    for (int i = 0; i < table.length; i++) for (Entry e = table[i]; e != null; e = e.next) {
      if (!first) b.append(", "); first = false; b.append(e.key).append('=').append(e.value);
    }
    return b.append('}').toString();
  }
}
