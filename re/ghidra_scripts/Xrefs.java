// args: <outfile> <addr> ...  : lists references to each address with the containing function
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
public class Xrefs extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    PrintWriter w = new PrintWriter(new FileWriter(a[0]));
    for (int i = 1; i < a.length; i++) {
      w.println("== refs to " + a[i]);
      for (Reference r : getReferencesTo(toAddr(a[i]))) {
        Function f = getFunctionContaining(r.getFromAddress());
        w.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
      }
    }
    w.close();
  }
}
