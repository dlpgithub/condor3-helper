// Headless: decompile functions to a text file.
// args: <outfile> <addr|addr..addr> ...   (each address: the function containing it)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
public class Decomp extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    PrintWriter w = new PrintWriter(new FileWriter(a[0]));
    DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
    for (int i = 1; i < a.length; i++) {
      Address ad = toAddr(a[i]);
      Function f = getFunctionContaining(ad);
      if (f == null) { w.println("// no function at " + a[i]); continue; }
      DecompileResults r = d.decompileFunction(f, 120, monitor);
      w.println("// ===== " + f.getName() + " @ " + f.getEntryPoint() + " (asked " + a[i] + ")");
      w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
    }
    w.close();
  }
}
