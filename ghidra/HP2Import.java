// Names Need for Speed: Hot Pursuit 2 (PS2) functions from static analysis.
// Reads functions_attributed.csv and sites.csv and applies them to the program.
//
// Put this file and the two CSVs in one folder, add that folder in Ghidra via
//   Window > Script Manager > Manage Script Directories > +
// then select HP2Import.java and press Run. Ghidra compiles it automatically;
// no Python or PyGhidra is involved.
//
//@category HP2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.util.HashMap;
import java.util.Map;

public class HP2Import extends GhidraScript {

    private Address at(long v) {
        return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(v);
    }

    private String baseName(String path) {
        String s = path.replace('\\', '/');
        int i = s.lastIndexOf('/');
        if (i >= 0) s = s.substring(i + 1);
        int d = s.lastIndexOf('.');
        if (d > 0) s = s.substring(0, d);
        return s.replaceAll("[^A-Za-z0-9_]", "_");
    }

    @Override
    public void run() throws Exception {
        File dir = askDirectory("Folder holding functions_attributed.csv and sites.csv", "Choose");

        File fFuncs = new File(dir, "functions_attributed.csv");
        File fSites = new File(dir, "sites.csv");
        if (!fFuncs.exists() || !fSites.exists()) {
            println("ERROR: expected functions_attributed.csv and sites.csv in " + dir);
            return;
        }

        int created = 0, named = 0, missed = 0;
        Map<String, Integer> seen = new HashMap<String, Integer>();

        BufferedReader r = new BufferedReader(new FileReader(fFuncs));
        String line = r.readLine();            // header
        while ((line = r.readLine()) != null) {
            if (monitor.isCancelled()) break;
            String[] c = line.split(",");
            if (c.length < 3) continue;
            long addr;
            try {
                addr = Long.parseLong(c[0].trim().replace("0x", ""), 16);
            } catch (NumberFormatException e) { continue; }
            String base = baseName(c[1].trim());
            String how = c[2].trim();

            Address a = at(addr);
            Function fn = getFunctionAt(a);
            if (fn == null) {
                try {
                    fn = createFunction(a, null);
                    if (fn != null) created++;
                } catch (Exception e) { /* not valid code here */ }
            }
            if (fn == null) { missed++; continue; }

            int n = seen.containsKey(base) ? seen.get(base) + 1 : 1;
            seen.put(base, n);
            try {
                fn.setName(String.format("%s_%02d_%08x", base, n, addr), SourceType.ANALYSIS);
                fn.setComment("source file: " + c[1].trim() + " (" + how + ")");
                named++;
            } catch (Exception e) { /* duplicate name, skip */ }
        }
        r.close();

        int commented = 0;
        Listing listing = currentProgram.getListing();
        r = new BufferedReader(new FileReader(fSites));
        line = r.readLine();                   // header
        while ((line = r.readLine()) != null) {
            if (monitor.isCancelled()) break;
            String[] c = line.split(",");
            if (c.length < 4) continue;
            long addr;
            try {
                addr = Long.parseLong(c[0].trim().replace("0x", ""), 16);
            } catch (NumberFormatException e) { continue; }
            String path = c[2].trim();
            String ln = c[3].trim();
            CodeUnit cu = listing.getCodeUnitAt(at(addr));
            if (cu != null) {
                cu.setComment(CodeUnit.PRE_COMMENT,
                        path + (ln.isEmpty() ? "" : ":" + ln));
                commented++;
            }
        }
        r.close();

        println("HP2Import: created " + created + " new functions, named " + named
                + ", commented " + commented + " file/line sites"
                + (missed > 0 ? ("  (" + missed + " addresses were not valid code)") : ""));
        println("Press Ctrl+S to save the program so the names persist.");
    }
}
