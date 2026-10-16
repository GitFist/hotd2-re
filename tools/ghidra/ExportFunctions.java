// Exports every function in the program: a CSV index plus one decompiled .c file per function.
// Run headless via tools/ghidra_analyze.ps1. Output dir is passed as the first script argument.
// @category hotd2-re

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportFunctions extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : "export");
		File cDir = new File(outDir, "functions");
		cDir.mkdirs();

		DecompInterface decomp = new DecompInterface();
		decomp.openProgram(currentProgram);

		int total = currentProgram.getFunctionManager().getFunctionCount();
		monitor.initialize(total);

		try (PrintWriter index = new PrintWriter(new File(outDir, "functions.csv"), "UTF-8")) {
			index.println("address,name,size,callers,callees,is_thunk,decompiled");
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				String addr = String.format("0x%08X", f.getEntryPoint().getOffset());
				boolean ok = false;

				if (!f.isThunk() && !f.isExternal()) {
					DecompileResults res = decomp.decompileFunction(f, 60, monitor);
					if (res != null && res.decompileCompleted()) {
						try (PrintWriter c = new PrintWriter(new File(cDir, addr + ".c"), "UTF-8")) {
							c.println("// " + addr + "  " + f.getName());
							c.print(res.getDecompiledFunction().getC());
						}
						ok = true;
					}
				}

				index.printf("%s,%s,%d,%d,%d,%b,%b%n", addr, f.getName(), f.getBody().getNumAddresses(),
					f.getCallingFunctions(monitor).size(), f.getCalledFunctions(monitor).size(),
					f.isThunk(), ok);
				monitor.incrementProgress(1);
			}
		}
		decomp.dispose();
		println("Exported " + total + " functions to " + outDir.getAbsolutePath());
	}
}
