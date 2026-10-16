// Exports cross-reference data for subsystem mapping:
//   imports.csv   – each imported API and every function that references it
//   strings.csv   – defined strings and the functions that reference them
//   callgraph.csv – caller -> callee edges between program functions
//   entry.txt     – program entry point
// Output dir is the first script argument.
// @category hotd2-re

import java.io.File;
import java.io.PrintWriter;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.ExternalManager;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class ExportXrefs extends GhidraScript {

	private static String hex(Address a) {
		return String.format("0x%08X", a.getOffset());
	}

	private static String csv(String s) {
		return "\"" + s.replace("\"", "\"\"").replace("\r", "\\r").replace("\n", "\\n") + "\"";
	}

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : "xrefs");
		outDir.mkdirs();

		try (PrintWriter w = new PrintWriter(new File(outDir, "entry.txt"), "UTF-8")) {
			for (Address a : currentProgram.getSymbolTable().getExternalEntryPointIterator()) {
				w.println(hex(a));
			}
		}

		// Imports: every reference to an external symbol (via IAT pointer or thunk).
		try (PrintWriter w = new PrintWriter(new File(outDir, "imports.csv"), "UTF-8")) {
			w.println("dll,api,ref_from,function,function_name");
			ExternalManager ext = currentProgram.getExternalManager();
			SymbolIterator syms = currentProgram.getSymbolTable().getExternalSymbols();
			while (syms.hasNext() && !monitor.isCancelled()) {
				Symbol s = syms.next();
				String dll = s.getParentNamespace().getName();
				for (Reference r : s.getReferences()) {
					emitRefsTo(w, dll, s.getName(), r.getFromAddress(), 0);
				}
			}
		}

		// Strings and the functions that use them.
		try (PrintWriter w = new PrintWriter(new File(outDir, "strings.csv"), "UTF-8")) {
			w.println("address,value,ref_from,function");
			DataIterator it = currentProgram.getListing().getDefinedData(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Data d = it.next();
				if (!d.hasStringValue()) {
					continue;
				}
				String value = String.valueOf(d.getValue());
				ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
				boolean any = false;
				while (refs.hasNext()) {
					Reference r = refs.next();
					Function f = getFunctionContaining(r.getFromAddress());
					w.printf("%s,%s,%s,%s%n", hex(d.getAddress()), csv(value), hex(r.getFromAddress()),
						f == null ? "" : hex(f.getEntryPoint()));
					any = true;
				}
				if (!any) {
					w.printf("%s,%s,,%n", hex(d.getAddress()), csv(value));
				}
			}
		}

		// Call graph.
		try (PrintWriter w = new PrintWriter(new File(outDir, "callgraph.csv"), "UTF-8")) {
			w.println("caller,callee");
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				Set<Function> callees = f.getCalledFunctions(monitor);
				for (Function c : callees) {
					w.printf("%s,%s%n", hex(f.getEntryPoint()), c.isExternal() ? "EXT:" + c.getName() : hex(c.getEntryPoint()));
				}
			}
		}
		println("xrefs exported to " + outDir.getAbsolutePath());
	}

	// Follows IAT slot -> thunk -> callers (bounded depth) and writes one row per referencing function.
	private void emitRefsTo(PrintWriter w, String dll, String api, Address from, int depth) throws Exception {
		Function f = getFunctionContaining(from);
		if (f != null && !f.isThunk()) {
			w.printf("%s,%s,%s,%s,%s%n", dll, api, hex(from), hex(f.getEntryPoint()), f.getName());
			return;
		}
		if (depth > 2) {
			return;
		}
		// IAT data slot or thunk: chase references to it.
		Address target = (f != null) ? f.getEntryPoint() : from;
		ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(target);
		while (refs.hasNext()) {
			emitRefsTo(w, dll, api, refs.next().getFromAddress(), depth + 1);
		}
	}
}
