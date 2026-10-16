// Lists every instruction that references the given data addresses (or ranges), with its function.
// Args: <outFile> <addr|start-end> [...]
// @category hotd2-re

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DataRefs extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		try (PrintWriter w = new PrintWriter(new File(args[0]), "UTF-8")) {
			w.println("target,ref_from,type,function,instruction");
			for (int i = 1; i < args.length; i++) {
				String[] parts = args[i].split("-");
				Address start = toAddr(parts[0].replaceFirst("^0x", ""));
				Address end = parts.length > 1 ? toAddr(parts[1].replaceFirst("^0x", "")) : start;
				for (Address a = start; a.compareTo(end) <= 0; a = a.add(1)) {
					ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(a);
					while (it.hasNext()) {
						Reference r = it.next();
						Function f = getFunctionContaining(r.getFromAddress());
						Instruction ins = getInstructionAt(r.getFromAddress());
						w.printf("0x%08X,0x%08X,%s,%s,\"%s\"%n", a.getOffset(), r.getFromAddress().getOffset(),
							r.getReferenceType(), f == null ? "" : String.format("0x%08X", f.getEntryPoint().getOffset()),
							ins == null ? "" : ins.toString());
					}
				}
			}
		}
	}
}
