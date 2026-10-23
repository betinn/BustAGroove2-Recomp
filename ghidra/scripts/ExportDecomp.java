// Headless post-script: dump function table (CSV) and decompiled C for every function.
// Output dir = script arg 0.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : "ghidra_out");
        out.mkdirs();
        DecompInterface ifc = new DecompInterface();
        ifc.setOptions(new DecompileOptions());
        ifc.openProgram(currentProgram);
        try (PrintWriter csv = new PrintWriter(new File(out, "functions.csv"));
             PrintWriter c = new PrintWriter(new File(out, "decomp.c"));
             PrintWriter syms = new PrintWriter(new File(out, "symbols.csv"))) {
            csv.println("address,size,name,namespace,calls_out");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            int n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                long size = f.getBody().getNumAddresses();
                csv.println(f.getEntryPoint() + "," + size + "," + f.getName() + "," + f.getParentNamespace().getName() + "," + f.getCalledFunctions(monitor).size());
                DecompileResults r = ifc.decompileFunction(f, 60, monitor);
                c.println("// ---- " + f.getEntryPoint() + " " + f.getName() + " (" + size + " bytes)");
                if (r != null && r.decompileCompleted()) c.println(r.getDecompiledFunction().getC());
                else c.println("// decompile failed: " + (r == null ? "null" : r.getErrorMessage()));
                n++;
            }
            syms.println("address,name,type");
            SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(false);
            while (si.hasNext()) {
                Symbol s = si.next();
                if (s.getSymbolType() == SymbolType.LABEL || s.getSymbolType() == SymbolType.FUNCTION)
                    syms.println(s.getAddress() + "," + s.getName() + "," + s.getSymbolType());
            }
            println("exported " + n + " functions to " + out.getAbsolutePath());
        }
        ifc.dispose();
    }
}
