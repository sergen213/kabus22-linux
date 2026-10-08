// Export address-backed loader/scheduler/plugin evidence without modifying the program.
// @category Kabus22
// Usage: RecoverKabus.java OUTPUT_DIRECTORY [MAX_FUNCTIONS=120] [TIMEOUT_SECONDS=20] [ADDRESS ...]
// Headless: analyzeHeadless PROJECT_DIRECTORY PROJECT_NAME -process '*' -noanalysis -readOnly
//           -scriptPath SCRIPT_DIRECTORY -postScript RecoverKabus.java OUTPUT_DIRECTORY

import java.io.BufferedWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.ExternalLocation;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.util.DefinedStringIterator;

public class RecoverKabus extends GhidraScript {
    private static final Pattern RELEVANT = Pattern.compile(
        "(?i)(wrs|wdl|acknex|gamestudio|_a4font|txt_load|str_read|delimit_str|resource|archive|pars(?:e|er|ing)|script|compil|" +
        "(?:^|[^a-z])(?:wait|proc|execute|version)(?:[^a-z]|$)|video_set|Direct3DCreate9|\\b[67]\\.\\d{2}\\b)");
    private final Map<Address, Function> candidates = new LinkedHashMap<>();
    private final Map<Address, Set<String>> reasons = new LinkedHashMap<>();
    private final Map<Address, Integer> priorities = new LinkedHashMap<>();
    private BufferedWriter report;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            throw new IllegalArgumentException("Expected OUTPUT_DIRECTORY [MAX_FUNCTIONS] [TIMEOUT_SECONDS] [ADDRESS ...]");
        }
        int maxFunctions = args.length > 1 ? Integer.parseInt(args[1]) : 120;
        int timeout = args.length > 2 ? Integer.parseInt(args[2]) : 20;
        if (maxFunctions < 1 || timeout < 1 || timeout > 300) {
            throw new IllegalArgumentException("MAX_FUNCTIONS must be positive; TIMEOUT_SECONDS must be 1..300");
        }
        Path directory = Path.of(args[0]);
        Files.createDirectories(directory);
        String name = currentProgram.getName().replaceAll("[^A-Za-z0-9._-]", "_");
        Path target = directory.resolve(name + ".recovery.txt");
        try (BufferedWriter writer = Files.newBufferedWriter(target, StandardCharsets.UTF_8)) {
            report = writer;
            metadata();
            imports();
            exports();
            strings();
            symbols();
            for (int i = 3; i < args.length; i++) {
                Address address = toAddr(args[i]);
                Function function = currentProgram.getFunctionManager().getFunctionContaining(address);
                if (function == null) {
                    line("EXPLICIT_ADDRESS_NOT_IN_FUNCTION " + address);
                    Data data = currentProgram.getListing().getDataContaining(address);
                    line("EXPLICIT_DATA " + address + " defined=" + (data == null ? "none" : data.toString()));
                    byte[] bytes = new byte[64];
                    int read = currentProgram.getMemory().getBytes(address, bytes);
                    StringBuilder hex = new StringBuilder();
                    for (int j = 0; j < read; j++) hex.append(String.format("%02x", bytes[j] & 255));
                    line("EXPLICIT_BYTES " + address + " " + hex);
                    references(address, "EXPLICIT_XREF", true, "explicit data address " + address, 500);
                } else {
                    select(function, "explicit address " + address, 1000);
                }
            }
            decompile(directory, name, maxFunctions, timeout);
            line("EXPORT_FINISHED cancellation=false");
        }
        println("Kabus recovery exported to " + target.toAbsolutePath());
    }

    private void metadata() throws IOException {
        line("PROGRAM " + currentProgram.getName());
        line("EXECUTABLE " + currentProgram.getExecutablePath());
        line("FORMAT " + currentProgram.getExecutableFormat());
        line("MD5 " + currentProgram.getExecutableMD5());
        line("SHA256 " + currentProgram.getExecutableSHA256());
        line("LANGUAGE " + currentProgram.getLanguageID());
        line("COMPILER_SPEC " + currentProgram.getCompilerSpec().getCompilerSpecID());
        line("IMAGE_BASE " + currentProgram.getImageBase());
        int external = 0, internal = 0, thunks = 0;
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext()) {
            Function function = functions.next();
            if (function.isExternal()) external++; else internal++;
            if (function.isThunk()) thunks++;
        }
        FunctionIterator externalFunctions = currentProgram.getFunctionManager().getExternalFunctions();
        int externalFunctionCount = 0;
        while (externalFunctions.hasNext()) { externalFunctions.next(); externalFunctionCount++; }
        line("FUNCTION_COUNTS internal=" + internal + " external_iterator=" + externalFunctionCount +
            " external_in_address_iterator=" + external + " thunks=" + thunks);
        line("SCOPE Existing Ghidra analysis; no binary edits. Candidates are evidence, not confirmed loader identities.");
        line("ADDRESS_KIND Program virtual addresses; EXTERNAL addresses are Ghidra synthetic, not runtime VAs.");
        line("DECOMPILATION Pseudocode is approximate; failures and omissions are reported explicitly.");
    }

    private void imports() throws Exception {
        line("\n=== IMPORTS ===");
        SymbolIterator symbols = currentProgram.getSymbolTable().getExternalSymbols();
        int count = 0;
        while (symbols.hasNext()) {
            monitor.checkCancelled();
            Symbol symbol = symbols.next();
            ExternalLocation location = currentProgram.getExternalManager().getExternalLocation(symbol);
            line("IMPORT " + symbol.getName(true) + " synthetic=" + symbol.getAddress() +
                " library=" + (location == null ? "unknown" : location.getLibraryName()) +
                " original_address=" + (location == null ? "unknown" : location.getAddress()));
            references(symbol.getAddress(), "IMPORT_XREF", false, "import " + symbol.getName(), 20);
            if (matches(symbol.getName())) {
                references(symbol.getAddress(), "TARGET_IMPORT_XREF", true, "import " + symbol.getName(), 80);
            }
            count++;
        }
        line("IMPORT_SYMBOL_COUNT " + count);
    }

    private void exports() throws Exception {
        line("\n=== EXTERNAL ENTRY POINTS / EXPORTS ===");
        AddressIterator entries = currentProgram.getSymbolTable().getExternalEntryPointIterator();
        int count = 0;
        while (entries.hasNext()) {
            monitor.checkCancelled();
            Address address = entries.next();
            Symbol symbol = currentProgram.getSymbolTable().getPrimarySymbol(address);
            Function function = currentProgram.getFunctionManager().getFunctionAt(address);
            line("EXPORT_OR_ENTRY " + address + " symbol=" + (symbol == null ? "none" : symbol.getName(true)) +
                " function=" + describe(function));
            for (Symbol alias : currentProgram.getSymbolTable().getSymbols(address)) {
                line("ENTRY_SYMBOL " + address + " name=" + alias.getName(true));
            }
            select(function, "external entry/export " + address, 200);
            if (function != null) {
                // One call edge only: exported wrappers often delegate plugin behavior.
                Function implementation = function.isThunk() ? function.getThunkedFunction(true) : function;
                if (implementation == null) implementation = function;
                for (Function called : implementation.getCalledFunctions(monitor)) {
                    select(called, "direct callee of export " + address, 100);
                }
            }
            count++;
        }
        line("EXTERNAL_ENTRY_COUNT " + count);
    }

    private void strings() throws Exception {
        line("\n=== RELEVANT DEFINED STRINGS ===");
        DataIterator data = DefinedStringIterator.forProgram(currentProgram);
        int all = 0, relevant = 0;
        while (data.hasNext()) {
            monitor.checkCancelled();
            Data value = data.next();
            String text = StringDataInstance.getStringDataInstance(value).getStringValue();
            if (text == null) continue;
            all++;
            if (!matches(text)) continue;
            relevant++;
            line("STRING " + value.getAddress() + " length=" + value.getLength() + " text=" + quote(text));
            references(value.getAddress(), "STRING_XREF", true, "string " + value.getAddress() + " " + quote(text), 150);
        }
        line("STRING_COUNTS defined=" + all + " relevant=" + relevant);
    }

    private void symbols() throws Exception {
        line("\n=== RELEVANT SYMBOLS ===");
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext()) {
            monitor.checkCancelled();
            Symbol symbol = symbols.next();
            if (symbol.isExternal() || !matches(symbol.getName())) continue;
            Function function = currentProgram.getFunctionManager().getFunctionContaining(symbol.getAddress());
            line("SYMBOL " + symbol.getAddress() + " name=" + symbol.getName(true) + " function=" + describe(function));
            select(function, "symbol " + symbol.getName(true), 150);
            references(symbol.getAddress(), "SYMBOL_XREF", true, "symbol " + symbol.getName(true), 90);
        }
    }

    private void references(Address address, String label, boolean select, String reason, int priority) throws Exception {
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(address);
        int count = 0;
        while (references.hasNext()) {
            monitor.checkCancelled();
            Reference reference = references.next();
            Address from = reference.getFromAddress();
            Function function = currentProgram.getFunctionManager().getFunctionContaining(from);
            line(label + " from=" + from + " to=" + address + " type=" + reference.getReferenceType() +
                " function=" + describe(function));
            if (select) select(function, reason + " via " + from, priority);
            // Resolve one level of pointer-table/IAT indirection; never invent a containing function.
            if (select && function == null && reference.getReferenceType().isData()) {
                ReferenceIterator indirect = currentProgram.getReferenceManager().getReferencesTo(from);
                while (indirect.hasNext()) {
                    monitor.checkCancelled();
                    Reference second = indirect.next();
                    Function owner = currentProgram.getFunctionManager().getFunctionContaining(second.getFromAddress());
                    line(label + "_INDIRECT from=" + second.getFromAddress() + " via=" + from +
                        " to=" + address + " type=" + second.getReferenceType() + " function=" + describe(owner));
                    select(owner, reason + " indirect via " + from, priority);
                }
            }
            count++;
        }
        if (count == 0) line(label + " to=" + address + " count=0");
    }

    private void select(Function function, String reason, int priority) {
        if (function == null || function.isExternal()) return;
        Address address = function.getEntryPoint();
        candidates.put(address, function);
        reasons.computeIfAbsent(address, key -> new LinkedHashSet<>()).add(reason);
        priorities.merge(address, priority, Math::max);
        if (function.isThunk()) {
            Function implementation = function.getThunkedFunction(true);
            if (implementation != null && !implementation.isExternal() && implementation != function) {
                Address target = implementation.getEntryPoint();
                candidates.put(target, implementation);
                reasons.computeIfAbsent(target, key -> new LinkedHashSet<>())
                    .add(reason + " resolved thunk " + address);
                priorities.merge(target, priority, Math::max);
            }
        }
    }

    private void decompile(Path directory, String name, int maximum, int timeout) throws Exception {
        line("\n=== TARGETED DECOMPILATION ===");
        List<Function> selected = new ArrayList<>(candidates.values());
        selected.sort(Comparator.<Function>comparingInt(f -> priorities.get(f.getEntryPoint())).reversed()
            .thenComparing(Function::getEntryPoint));
        line("CANDIDATE_COUNT " + selected.size() + " limit=" + maximum + " timeout_seconds=" + timeout);
        DecompInterface decompiler = new DecompInterface();
        int completed = 0, failed = 0, omitted = 0;
        try {
            if (!decompiler.openProgram(currentProgram)) {
                throw new IOException("Decompiler could not open program: " + decompiler.getLastMessage());
            }
            for (int i = 0; i < selected.size(); i++) {
                monitor.checkCancelled();
                Function function = selected.get(i);
                Address entry = function.getEntryPoint();
                line("CANDIDATE " + describe(function) + " priority=" + priorities.get(entry));
                for (String reason : reasons.get(entry)) line("  REASON " + reason);
                if (i >= maximum) {
                    line("  STATUS OMITTED_SELECTION_LIMIT");
                    omitted++;
                    continue;
                }
                DecompileResults result = decompiler.decompileFunction(function, timeout, monitor);
                if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
                    line("  STATUS FAILED timed_out=" + result.isTimedOut() + " cancelled=" + result.isCancelled() +
                        " error=" + quote(result.getErrorMessage()));
                    failed++;
                    continue;
                }
                Path output = directory.resolve(name + "." + entry + ".c");
                Files.writeString(output, "/* Ghidra approximation: " + describe(function) +
                    "\n * Source program: " + currentProgram.getName() + "\n */\n" +
                    result.getDecompiledFunction().getC(), StandardCharsets.UTF_8);
                line("  STATUS COMPLETED output=" + output.getFileName() + " diagnostic=" + quote(result.getErrorMessage()));
                completed++;
            }
        } finally {
            decompiler.dispose();
            line("DECOMPILE_COUNTS completed=" + completed + " failed=" + failed + " omitted=" + omitted);
        }
    }

    private boolean matches(String text) { return RELEVANT.matcher(text).find(); }
    private String describe(Function function) {
        return function == null ? "none" : function.getName(true) + "@" + function.getEntryPoint();
    }
    private String quote(String text) {
        if (text == null) return "null";
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"")
            .replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t") + "\"";
    }
    private void line(String text) throws IOException { report.write(text); report.newLine(); }
}
