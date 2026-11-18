package javax.microedition.rms;

import java.util.Hashtable;

/**
 * RecordStore logic with no device: the store's files go to RecordStore.files (a table) instead of Native.
 * Runs on a PC JDK (j2me/test/midp/run-host.sh) or the VM (j2me-run); prints "RmsTest: OK (n checks)".
 */
public class RmsTest {
    static int checks, fails;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            fails++;
            System.out.println("FAIL " + what);
        }
    }

    static void eq(long got, long want, String what) {
        checks++;
        if (got != want) {
            fails++;
            System.out.println("FAIL " + what + ": got " + got + " want " + want);
        }
    }

    static void eq(String got, String want, String what) {
        checks++;
        if (got == null ? want != null : !got.equals(want)) {
            fails++;
            System.out.println("FAIL " + what + ": got " + got + " want " + want);
        }
    }

    static byte[] b(String s) { return s.getBytes(); }

    static boolean same(byte[] x, byte[] y) {
        if (x.length != y.length) return false;
        for (int i = 0; i < x.length; i++) if (x[i] != y[i]) return false;
        return true;
    }

    static String s(byte[] b) { return b == null ? null : new String(b); }

    static byte[] num(int v) { return new byte[] {(byte) (v >> 8), (byte) v}; }

    static int num(byte[] b) { return ((b[0] & 0xFF) << 8) | (b[1] & 0xFF); }

    public static void main(String[] a) throws Exception {
        RecordStore.files = new Hashtable();
        basics();
        persistence();
        sharing();
        enumerations();
        listeners();
        errors();
        if (fails == 0) System.out.println("RmsTest: OK (" + checks + " checks)");
        else System.out.println("RmsTest: FAILED " + fails + " of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }

    static void basics() throws Exception {
        RecordStore rs = RecordStore.openRecordStore("s1", true);
        String[] l = RecordStore.listRecordStores();
        check(l != null && l.length == 1 && l[0].equals("s1"), "listRecordStores after create");
        eq(rs.getNumRecords(), 0, "new store empty");
        eq(rs.getNextRecordID(), 1, "first id");
        eq(rs.getName(), "s1", "name");
        eq(rs.addRecord(b("abc"), 0, 3), 1, "add abc");
        eq(rs.addRecord(null, 0, 0), 2, "add null (empty record)");
        eq(rs.addRecord(new byte[0], 0, 0), 3, "add 0 bytes");
        eq(rs.addRecord(b("xxhelloxx"), 2, 5), 4, "add with offset");
        check(rs.getRecord(2) == null, "an empty record reads as null");
        eq(rs.getRecordSize(2), 0, "empty record size");
        eq(s(rs.getRecord(1)), "abc", "record 1");
        eq(s(rs.getRecord(4)), "hello", "record 4 (offset)");
        byte[] buf = new byte[8];
        eq(rs.getRecord(1, buf, 2), 3, "getRecord into buffer: length");
        eq(new String(buf, 2, 3), "abc", "getRecord into buffer: bytes");
        try {
            rs.getRecord(1, new byte[2], 0);
            check(false, "small buffer: no exception");
        } catch (ArrayIndexOutOfBoundsException e) {
            check(true, "");
        }
        byte[] r1 = rs.getRecord(1);
        r1[0] = 'z';
        eq(s(rs.getRecord(1)), "abc", "getRecord gives a copy");
        rs.deleteRecord(3);
        eq(rs.getNumRecords(), 3, "count after delete");
        eq(rs.getNextRecordID(), 5, "ids are not reused");
        eq(rs.addRecord(b("x"), 0, 1), 5, "next id after delete");
        rs.setRecord(1, b("ABCD"), 0, 4);
        eq(s(rs.getRecord(1)), "ABCD", "setRecord");
        eq(rs.getRecordSize(1), 4, "size after setRecord");
        eq(rs.getVersion(), 7, "version counts changes");
        check(rs.getLastModified() > 0, "lastModified set");
        eq(rs.getSizeAvailable(), RecordStore.MAX_SIZE - rs.getSize(), "size available");
        // the file format
        byte[] file = (byte[]) RecordStore.files.get("s1");
        check(file != null && file.length == rs.getSize(), "saved after each change, size = file size");
        RecordStore d = RecordStore.decode("s1", rs.encode());
        eq(d.count, 4, "decode: count");
        eq(d.nextId, 6, "decode: nextId");
        eq(d.version, 7, "decode: version");
        eq(d.lastModified, rs.getLastModified(), "decode: lastModified");
        eq(d.ids[0] * 1000 + d.ids[1] * 100 + d.ids[2] * 10 + d.ids[3], 1245, "decode: ids");
        eq(s(d.data[0]), "ABCD", "decode: record 1");
        eq(d.data[1].length, 0, "decode: empty record");
        eq(s(d.data[2]), "hello", "decode: record 4");
        check(same(d.encode(), rs.encode()), "encode(decode(x)) == x");
        rs.closeRecordStore();
    }

    static void persistence() throws Exception {
        RecordStore rs = RecordStore.openRecordStore("s1", false);
        eq(rs.getNumRecords(), 4, "reopen: records");
        eq(rs.getNextRecordID(), 6, "reopen: nextId kept");
        eq(rs.getVersion(), 7, "reopen: version kept");
        check(rs.getRecord(2) == null, "reopen: empty record");
        eq(s(rs.getRecord(5)), "x", "reopen: record 5");
        eq(rs.addRecord(b("y"), 0, 1), 6, "reopen: next id continues");
        rs.closeRecordStore();
        // a damaged file
        RecordStore.files.put("bad", new byte[] {1, 2, 3});
        try {
            RecordStore.openRecordStore("bad", true);
            check(false, "damaged file: no exception");
        } catch (RecordStoreException e) {
            check(!(e instanceof RecordStoreNotFoundException), "damaged file: RecordStoreException");
        }
        RecordStore.files.remove("bad");
        // a format from the future
        RecordStore fu = RecordStore.openRecordStore("future", true);
        byte[] f = fu.encode();
        fu.closeRecordStore();
        f[4] = 9;
        RecordStore.files.put("other", f);
        try {
            RecordStore.openRecordStore("other", false);
            check(false, "unknown format: no exception");
        } catch (RecordStoreException e) {
            check(true, "");
        }
        RecordStore.files.remove("other");
    }

    static void sharing() throws Exception {
        RecordStore a = RecordStore.openRecordStore("shared", true);
        RecordStore b = RecordStore.openRecordStore("shared", true);
        check(a == b, "one instance a name");
        a.addRecord(b("1"), 0, 1);
        eq(b.getNumRecords(), 1, "both openers see it");
        a.closeRecordStore();
        eq(b.getNumRecords(), 1, "still open after one close");
        try {
            RecordStore.deleteRecordStore("shared");
            check(false, "delete while open: no exception");
        } catch (RecordStoreNotFoundException e) {
            check(false, "delete while open: wrong exception (not found)");
        } catch (RecordStoreException e) {
            check(true, "");
        }
        b.closeRecordStore();
        try {
            b.getNumRecords();
            check(false, "closed: no exception");
        } catch (RecordStoreNotOpenException e) {
            check(true, "");
        }
        try {
            b.closeRecordStore();
            check(false, "close twice: no exception");
        } catch (RecordStoreNotOpenException e) {
            check(true, "");
        }
        RecordStore.deleteRecordStore("shared");
        try {
            RecordStore.openRecordStore("shared", false);
            check(false, "deleted store opens");
        } catch (RecordStoreNotFoundException e) {
            check(true, "");
        }
        try {
            RecordStore.deleteRecordStore("shared");
            check(false, "delete twice: no exception");
        } catch (RecordStoreNotFoundException e) {
            check(true, "");
        }
        RecordStore.deleteRecordStore("s1");
        RecordStore.deleteRecordStore("future");
        check(RecordStore.listRecordStores() == null, "no stores: listRecordStores is null");
    }

    static String order(RecordEnumeration e) throws Exception {
        StringBuffer sb = new StringBuffer();
        e.reset();
        while (e.hasNextElement()) sb.append(num(e.nextRecord())).append(' ');
        return sb.toString().trim();
    }

    static void enumerations() throws Exception {
        RecordStore rs = RecordStore.openRecordStore("scores", true);
        int[] v = {5, 3, 9, 1};
        for (int i = 0; i < v.length; i++) rs.addRecord(num(v[i]), 0, 2);  // ids 1..4
        RecordFilter atLeast3 = new RecordFilter() {
            public boolean matches(byte[] c) { return num(c) >= 3; }
        };
        RecordComparator high = new RecordComparator() {
            public int compare(byte[] x, byte[] y) {
                int a = num(x), b = num(y);
                return a > b ? PRECEDES : a < b ? FOLLOWS : EQUIVALENT;
            }
        };
        RecordEnumeration plain = rs.enumerateRecords(null, null, false);
        eq(order(plain), "5 3 9 1", "no comparator: ascending ids");
        RecordEnumeration e = rs.enumerateRecords(atLeast3, high, false);
        eq(e.numRecords(), 3, "filter");
        eq(order(e), "9 5 3", "filter + comparator");
        e.reset();
        eq(e.nextRecordId(), 3, "first id (9)");
        eq(e.nextRecordId(), 1, "second id (5)");
        eq(e.previousRecordId(), 3, "previous goes back");
        try {
            e.previousRecordId();
            check(false, "previous before the first: no exception");
        } catch (InvalidRecordIDException x) {
            check(true, "");
        }
        e.reset();
        eq(e.previousRecordId(), 2, "previous after reset: the last (3)");
        check(e.hasPreviousElement() && !e.hasNextElement(), "at the end: has previous, no next");
        RecordEnumeration live = rs.enumerateRecords(atLeast3, high, true);
        check(live.isKeptUpdated(), "kept updated");
        int id7 = rs.addRecord(num(7), 0, 2);
        eq(order(live), "9 7 5 3", "kept updated: add");
        eq(order(e), "9 5 3", "not kept: unchanged");
        e.rebuild();
        eq(order(e), "9 7 5 3", "rebuild");
        rs.deleteRecord(3);  // the 9
        eq(order(live), "7 5 3", "kept updated: delete");
        rs.setRecord(id7, num(2), 0, 2);  // 7 -> 2: filtered out now
        eq(order(live), "5 3", "kept updated: change");
        live.reset();
        eq(live.nextRecordId(), 1, "cursor on 5");
        rs.addRecord(num(8), 0, 2);
        eq(live.nextRecordId(), 2, "cursor kept on its record after an add before it");
        live.keepUpdated(false);
        rs.addRecord(num(6), 0, 2);
        eq(live.numRecords(), 3, "no longer updated");
        live.destroy();
        try {
            live.numRecords();
            check(false, "destroyed: no exception");
        } catch (IllegalStateException x) {
            check(true, "");
        }
        rs.closeRecordStore();
        try {
            e.reset();
            e.nextRecord();
            check(false, "closed store: nextRecord gives no exception");
        } catch (RecordStoreNotOpenException x) {
            check(true, "");
        }
        check(!e.hasNextElement(), "closed store: no next");
        RecordStore.deleteRecordStore("scores");
    }

    static void listeners() throws Exception {
        RecordStore rs = RecordStore.openRecordStore("ls", true);
        final StringBuffer log = new StringBuffer();
        RecordListener l = new RecordListener() {
            public void recordAdded(RecordStore r, int id) { log.append("a").append(id); }
            public void recordChanged(RecordStore r, int id) { log.append("c").append(id); }
            public void recordDeleted(RecordStore r, int id) { log.append("d").append(id); }
        };
        rs.addRecordListener(l);
        rs.addRecordListener(l);
        int id = rs.addRecord(b("q"), 0, 1);
        rs.setRecord(id, b("r"), 0, 1);
        rs.deleteRecord(id);
        rs.removeRecordListener(l);
        rs.addRecord(b("s"), 0, 1);
        eq(log.toString(), "a1c1d1", "listener calls (once each, none after remove)");
        rs.closeRecordStore();
        RecordStore.deleteRecordStore("ls");
    }

    static void errors() throws Exception {
        RecordStore rs = RecordStore.openRecordStore("err", true);
        try {
            rs.getRecord(99);
            check(false, "bad id: no exception");
        } catch (InvalidRecordIDException e) {
            check(true, "");
        }
        try {
            rs.deleteRecord(0);
            check(false, "id 0: no exception");
        } catch (InvalidRecordIDException e) {
            check(true, "");
        }
        try {
            rs.addRecord(b("ab"), 1, 5);
            check(false, "bad range: no exception");
        } catch (ArrayIndexOutOfBoundsException e) {
            check(true, "");
        }
        try {
            rs.addRecord(new byte[RecordStore.MAX_SIZE], 0, RecordStore.MAX_SIZE);
            check(false, "too big: no exception");
        } catch (RecordStoreFullException e) {
            check(true, "");
        }
        rs.closeRecordStore();
        try {
            rs.addRecord(b("a"), 0, 1);
            check(false, "add after close: no exception");
        } catch (RecordStoreNotOpenException e) {
            check(true, "");
        }
        try {
            RecordStore.openRecordStore("123456789012345678901234567890123", true);
            check(false, "33 char name: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        try {
            RecordStore.openRecordStore("", true);
            check(false, "empty name: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        try {
            RecordStore.openRecordStore("nope", false);
            check(false, "missing store: no exception");
        } catch (RecordStoreNotFoundException e) {
            check(true, "");
        }
        RecordStore.deleteRecordStore("err");
    }
}
