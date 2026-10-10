#pragma once
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
namespace k22 {
class Engine;class Runtime;class Renderer;struct Input;
// Scripted gameplay run (docs/GAMEPLAY_TESTING.md): timed input, waits and assertions on the live WDL state.
class Scenario {
public:
 Scenario(const std::filesystem::path& file,std::filesystem::path output);
 ~Scenario();
 // Before each tick: runs instantaneous steps, then owns the frame's keyboard/mouse/joystick input. False once finished.
 bool step(Engine&,Runtime&,Renderer*,Input&,std::uint64_t frame);
 bool realtime()const;
 // "seed N" (default 0): the WDL random() stream; scenario mode never reseeds it from the OS (randomize).
 std::uint32_t seed()const;
 // Prints the summary; returns the process exit code. quit: the engine ended the run itself (expect-exit).
 int finish(std::ostream&,std::uint64_t frames,bool quit=false);
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
