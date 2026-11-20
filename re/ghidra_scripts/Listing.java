// args: <outfile> <start> <end> [<start> <end> ...] : instruction listing of address ranges
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
public class Listing extends GhidraScript {
  public void run() throws Exception {
    String[] a = getScriptArgs();
    PrintWriter w = new PrintWriter(new FileWriter(a[0]));
    for (int i = 1; i + 1 < a.length; i += 2) {
      w.println("== " + a[i] + " .. " + a[i+1]);
      InstructionIterator it = currentProgram.getListing().getInstructions(new AddressSet(toAddr(a[i]), toAddr(a[i+1])), true);
      while (it.hasNext()) { Instruction ins = it.next(); w.println("  " + ins.getAddress() + "  " + ins); }
    }
    w.close();
  }
}
