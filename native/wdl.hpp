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
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
