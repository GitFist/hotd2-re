// Creates functions at the given addresses if missing, then writes decompiled C for each.
// Args: <outDir> <addr> [addr...]   (addresses in hex, with or without 0x)
// Run without -readOnly to keep the created functions in the project.
// @category hotd2-re

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompileAt extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args[0]);
		outDir.mkdirs();

		DecompInterface decomp = new DecompInterface();
		decomp.openProgram(currentProgram);

		for (int i = 1; i < args.length; i++) {
			Address a = toAddr(args[i].replaceFirst("^0x", ""));
			Function f = getFunctionAt(a);
			if (f == null) {
				new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
				new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
				f = getFunctionAt(a);
				println("created function at " + a + ": " + (f != null));
			}
			if (f == null) {
				continue;
			}
			String name = String.format("0x%08X", a.getOffset());
			DecompileResults res = decomp.decompileFunction(f, 60, monitor);
			if (res != null && res.decompileCompleted()) {
				try (PrintWriter w = new PrintWriter(new File(outDir, name + ".c"), "UTF-8")) {
					w.println("// " + name + "  " + f.getName());
					w.print(res.getDecompiledFunction().getC());
				}
			}
		}
		decomp.dispose();
	}
}
