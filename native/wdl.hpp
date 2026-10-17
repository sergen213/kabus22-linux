#pragma once
#include "value.hpp"
#include <functional>
#include <memory>
namespace k22 {
struct ParseMetrics {std::size_t files=0,lines=0,tokens=0,declarations=0,functions=0,statements=0,expressions=0,directives=0;};
class Runtime {
public:
 struct CompilerSettings {std::string entry;std::string plugin_directory;std::vector<std::string> bindings,levels;};
 using Source=std::function<std::string(std::string_view)>;
 using Builtin=std::function<Value(Runtime&,ProcessContext&,std::string_view,std::span<const Argument>)>;
 using Resource=std::function<void(std::string_view)>;
 Runtime(State&,Builtin); ~Runtime(); Runtime(const Runtime&)=delete; Runtime& operator=(const Runtime&)=delete;
 void load(std::string_view entry,Source,Resource={});
 void load_text(std::string_view name,std::string_view text);
 // Suspended reference arguments lease their slots independently of the caller's context.
 Value start(FunctionId,std::span<const Argument> args={},ObjectId my=0,ObjectId you=0);
 Value start(std::string_view,std::span<const Argument> args={},ObjectId my=0,ObjectId you=0);
 void enqueue(FunctionId,std::span<const Argument> args={},ObjectId my=0,ObjectId you=0);
 void bootstrap();
 void tick(std::int32_t raw_time,std::int32_t freeze_mode);
 void kill_for_entity(ObjectId); void late_current();
 State& state(); FunctionId function(std::string_view)const;
 bool external_import(std::string_view)const;
 const std::vector<std::string>& diagnostics()const; const ParseMetrics& metrics()const;
 const CompilerSettings& compiler_settings()const;
 std::size_t processes()const;
 bool running(FunctionId)const; // a live suspended process of this function is queued
 // Native override: reads of GLOBAL in FUNCTIONS, in every function defined in FILES (script file names), and in the index of
 // ARRAYS[...] anywhere, become the native call CALL(); returns the count.
 std::size_t reroute_reads(std::string_view global,std::string_view call,std::span<const std::string_view> functions,std::span<const std::string_view> arrays={},std::span<const std::string_view> files={});
 // Native override: in FUNCTION, a statement `X.FIELD = GLOBAL` assigns the native call CALL() instead; returns the count.
 std::size_t reroute_assigned_read(std::string_view function,std::string_view field,std::string_view global,std::string_view call);
 // Coop: per-player global banks (docs/COOP.md). Off until enable_banks; zero overhead when off.
 static constexpr int BankDynamic=-1,BankSkip=-2;
 // resolver(function,my,inherited): inherited = the active process's bank (0 if none) when a process is prepared (start/enqueue/
 // builtin-spawned), or BankDynamic when re-asked at a resume. Returns 0 or 1; BankDynamic (prepare only): re-ask at every resume;
 // BankSkip (prepare only): the call does not run and returns 0.
 using BankResolver=std::function<int(FunctionId,ObjectId my,int inherited)>;
 // Two banks. NAMES are globals (scalars, pointer cells such as ENTITY* player, and vector/array/string globals element-wise,
 // never by moving storage — Refs into elements must stay valid) whose live values belong to the running process's bank.
 // run() swaps on entry/exit/exception when the bank differs; between processes bank 0 is live. Empty NAMES: banks off.
 // Bank 1 starts as a copy of the current values. load()/load_text() turn banks off.
 void enable_banks(std::span<const std::string> names,BankResolver);
 int active_bank()const;                       // bank whose values are live now (0 between processes)
 Value bank_value(int bank,std::string_view global)const;          // scalar/pointer value as stored for that bank
 void set_bank_value(int bank,std::string_view global,Value);
 std::vector<std::string> assigned_globals(std::span<const std::string_view> files)const; // globals assigned (=,+=,++,vec_set target…) in functions defined in FILES
 std::string_view function_file(FunctionId)const; std::string_view function_name(FunctionId)const;
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
