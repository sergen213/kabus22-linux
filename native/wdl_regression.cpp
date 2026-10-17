#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wdl.hpp"
#include "engine.hpp"
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <optional>
#include <system_error>
#include <unistd.h>

namespace {
using namespace k22;
struct Event {
    ProcessContext context;
    std::vector<std::int32_t> values;
};
struct Fixture {
    State state;
    std::vector<Event> events;
    Runtime runtime{state, [this](Runtime& vm, ProcessContext& context,
                                 std::string_view name, std::span<const Argument> args) {
        if (name == "check") {
            assert(args.size() == 2);
            const auto actual = args[0].current(), expected = args[1].current();
            assert(actual.kind == expected.kind && actual.bits == expected.bits);
        } else if (name == "emit") {
            Event event{context, {}};
            for (const auto& arg : args) {
                const auto value = arg.current();
                assert(value.kind == Value::Kind::Number);
                event.values.push_back(value.raw());
            }
            events.push_back(std::move(event));
        } else if (name == "rebind_context") {
            assert(args.size() == 2);
            context.my = args[0].current().bits;
            context.you = args[1].current().bits;
        } else if (name == "remove_actor") {
            assert(args.size() == 1);
            const auto id = args[0].current().bits;
            vm.kill_for_entity(id);
            state.retire(id);
        } else if (name == "set_text") {
            assert(args.size() == 2);
            state.require(args[0].current().bits).text = std::to_string(args[1].current().integer());
        } else {
            throw std::runtime_error("unexpected regression builtin: " + std::string(name));
        }
        return Value{};
    }};

    Fixture() { state.global("time").value = Value::from_raw(64); }
    void load(std::string_view source) {
        runtime.load_text("wdl_regression.wdl", source);
        assert(runtime.diagnostics().empty());
    }
    void start(std::string_view name, std::initializer_list<std::int32_t> raw_args = {},
               ObjectId my = 0, ObjectId you = 0) {
        std::vector<Argument> args;
        for (auto raw : raw_args) args.push_back({Value::from_raw(raw), {}});
        runtime.start(name, args, my, you);
    }
    void expect(std::size_t index, std::initializer_list<std::int32_t> raw_values,
                ObjectId my = 0, ObjectId you = 0) const {
        assert(index < events.size());
        assert(events[index].values == std::vector<std::int32_t>(raw_values));
        assert(events[index].context.my == my && events[index].context.you == you);
    }
    ObjectId entity(std::int32_t skill) {
        auto& object = state.create(Object::Kind::Entity);
        object.field("skill1").value = Value::from_int(skill);
        return object.id;
    }
};
constexpr std::int32_t raw(std::int32_t integer) { return integer * 1024; }

void arithmetic_and_aliases() {
    Fixture f;
    const auto entity = f.entity(7);
    f.load(R"wdl(
        DEFINE Alias,total;
        DEFINE First,skill1;
        DEFINE Twice,2;
        var total = 0;
        function probe() { total += 1; return 1; }
        starter dormant() { emit(99); }
        function arithmetic(a,b) {
            Alias = (Twice + 3) * 4 - 10 / 2;
            check(total,15);
            check(my.First,7);
            var logical = 0 && probe();
            check(logical,0);
            logical = 1 || probe();
            check(logical,1);
            check(total,17);
            emit(0.1,-0.1,0.0009,-0.0009,1/1024,-1/3);
            emit(a*b,-0.5*0.00390625);
            emit(2097151.9990234375 + 0.0009765625,-(-2097152));
        }
    )wdl");
    assert(f.events.empty()); // load_text does not implicitly run starters.
    f.start("arithmetic", {-641, 6700417}, entity);
    assert(f.events.size() == 3);
    f.expect(0, {102, -102, 0, 0, 1, -341}, entity);
    // The recovered multiplier floors products below -2^32, not all negatives.
    f.expect(1, {-4194305, -2}, entity);
    f.expect(2, {std::numeric_limits<std::int32_t>::min(),
                 std::numeric_limits<std::int32_t>::min()}, entity);
    f.start("dormant");
    assert(f.events.size() == 4);
    f.expect(3, {raw(99)});
    assert(f.runtime.processes() == 0);
    assert(Value::from_double(-0.1).raw() == -102);
    assert(Value::from_raw(-102).integer() == 0);
}

void filename_tokens_preserve_macro_boundaries() {
    Fixture f;
    std::vector<std::string> sources, resources;
    f.runtime.load("entry.wdl", [&](std::string_view name) -> std::string {
        sources.emplace_back(name);
        if (name == "entry.wdl") return R"wdl(
            DEFINE Pompali,2;
            var before = Pompali;
            INCLUDE <sounds.wdl>;
            var after = Pompali;
            function main() {
                check(before,2); check(after,3);
                check(before < after,1); check(after > before,1);
                check(before > after,0); check(after < before,0);
                emit(before,after);
            }
        )wdl";
        if (name == "sounds.wdl") return R"wdl(
            SOUND first = <pompali.ogg>;
            INCLUDE <shadow.wdl>;
            SOUND second = <pompali.ogg>;
            SOUND implicit_name <effects/pompali.ogg>;
        )wdl";
        if (name == "shadow.wdl") return R"wdl(DEFINE Pompali,3;
            SOUND changed = <pompali.ogg>;
            DEFINE FilenameAlias,"pompali.ogg";
            SOUND alias = <FilenameAlias>;
            RESOURCE <pompali.ogg>;
        )wdl";
        throw std::runtime_error("unexpected regression source: " + std::string(name));
    }, [&](std::string_view name) { resources.emplace_back(name); });
    assert((sources == std::vector<std::string>{"entry.wdl", "sounds.wdl", "shadow.wdl"}));
    assert((resources == std::vector<std::string>{"pompali.ogg"}));
    assert(f.runtime.metrics().files == 3);
    assert(f.runtime.diagnostics().size() == 1);
    assert(f.runtime.diagnostics()[0] == "shadow.wdl:1:1: redefining DEFINE pompali");
    for (const auto name : {"first", "second", "changed", "alias"}) {
        const auto value = f.state.global(name).value;
        assert(value.kind == Value::Kind::Object);
        const auto& sound = f.state.require(value.bits);
        assert(sound.kind == Object::Kind::Sound && sound.text == "pompali.ogg");
    }
    const auto implicit = f.state.global("implicit_name").value;
    assert(implicit.kind == Value::Kind::Object);
    const auto& sound = f.state.require(implicit.bits);
    assert(sound.kind == Object::Kind::Sound && sound.text == "effects/pompali.ogg");
    assert(f.events.empty());
    f.start("main");
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(2), raw(3)});
}

void literal_delimiters_reach_file_text_consumers() {
    Fixture f;
    f.load(R"wdl(
        #DEFINE enabled,1;
        DEFINE semicolon,";";
        DEFINE same,"same";
        #IFDEF enabled;
        text punctuation { strings=3; string = "," semicolon "+"; }
        function pass(value) { return(value); }
        function delimiter(which) {
            if (which == 0) return(pass("#"));
            if (which == 1) return(pass(")"));
            if (which == 2) return(punctuation.string[0]);
            if (which == 3) return(punctuation.string[1]);
            if (which == 4) return(punctuation.string[2]);
            if (which == 5) return("endif");
            if (which == 6) return("define");
            return(same);
        }
        #IFELSE;
        function delimiter(which) { return("wrong"); }
        #ENDIF;
    )wdl");
    const char* separators[] = {"#", ")", ",", ";", "+", "endif", "define", "same"};
    for (std::size_t i = 0; i < std::size(separators); ++i) {
        const Argument argument{Value::from_int(i), {}};
        const auto result = f.runtime.start("delimiter", std::span(&argument, 1));
        assert(result.kind == Value::Kind::Object);
        const auto& delimiter = f.state.require(result.bits).text;
        assert(delimiter == separators[i]);
        if (delimiter.size() != 1) continue;
        // Consume the script-selected delimiter, as the archived settings reader does.
        std::istringstream file_text("1920" + delimiter + "3" + delimiter + "2");
        std::string field;
        const int expected[] = {1920, 3, 2};
        std::size_t field_index = 0;
        while (std::getline(file_text, field, delimiter.front())) {
            assert(field_index < std::size(expected));
            assert(std::stoi(field) == expected[field_index++]);
        }
        assert(field_index == std::size(expected));
    }
}

void child_first_wait_returns() {
    Fixture f;
    f.load(R"wdl(
        function child(arg) { var local = arg; emit(1); wait(1); emit(3,local,arg); }
        function caller() { emit(0); var result = child(7); emit(2,result); }
    )wdl");
    f.start("caller");
    assert(f.events.size() == 3 && f.runtime.processes() == 1);
    f.expect(0, {0});
    f.expect(1, {raw(1)});
    f.expect(2, {raw(2), raw(1)});
    f.runtime.tick(64, 0);
    assert(f.events.size() == 4 && f.runtime.processes() == 0);
    f.expect(3, {raw(3), raw(7), raw(7)});
}
// A6 00410a80 case5: an IF/WHILE condition is the last accumulator write, so a function that falls off its end after a false
// IF returns 0, not an earlier statement value (kotu_koruyucu.wdl Kotukoru_OneKontrol: one_yapma_sayisi+=1; if(==10){...}).
void falling_off_returns_last_condition() {
    Fixture f;
    f.load(R"wdl(
        var counter = 0;
        function counted() { counter += 1; if (counter == 10) { return(5); } }
        function looped() { var i = 3; while (i > 0) { i -= 1; } }
        function caller() { emit(counted(), looped()); counter = 9; emit(counted()); }
    )wdl");
    f.start("caller");
    f.expect(0, {0, 0});
    f.expect(1, {raw(5)});
}

void independent_local_arguments_and_context() {
    Fixture f;
    const auto a = f.entity(101), b = f.entity(102);
    const auto c = f.entity(103), d = f.entity(104);
    f.load(R"wdl(
        function worker(tag,amount) {
            var local = amount;
            emit(tag,local,my.skill1,you.skill1);
            wait(1);
            local += tag;
            my.skill2 = local;
            emit(tag,amount,local,my.skill1,you.skill1);
            wait(1);
            emit(tag,amount,local,my.skill2);
        }
    )wdl");
    f.start("worker", {raw(1), raw(10)}, a, b);
    f.start("worker", {raw(2), raw(20)}, c, d);
    assert(f.events.size() == 2 && f.runtime.processes() == 2);
    f.expect(0, {raw(1), raw(10), raw(101), raw(102)}, a, b);
    f.expect(1, {raw(2), raw(20), raw(103), raw(104)}, c, d);
    const auto first_process = f.events[0].context.processId;
    const auto second_process = f.events[1].context.processId;
    assert(first_process != second_process);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 4 && f.runtime.processes() == 2);
    f.expect(2, {raw(1), raw(10), raw(11), raw(101), raw(102)}, a, b);
    f.expect(3, {raw(2), raw(20), raw(22), raw(103), raw(104)}, c, d);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 6 && f.runtime.processes() == 0);
    f.expect(4, {raw(1), raw(10), raw(11), raw(11)}, a, b);
    f.expect(5, {raw(2), raw(20), raw(22), raw(22)}, c, d);
    assert(f.events[2].context.processId == first_process);
    assert(f.events[4].context.processId == first_process);
    assert(f.events[3].context.processId == second_process);
    assert(f.events[5].context.processId == second_process);
}

void wait_clamps_to_one_frame() {
    Fixture f;
    f.load(R"wdl(function frame(tag,delay) { wait(delay); emit(tag); })wdl");
    f.start("frame", {raw(1), raw(-16)});
    f.start("frame", {raw(2), 0});
    f.start("frame", {raw(3), 512});
    f.start("frame", {raw(4), 1024});
    assert(f.events.empty() && f.runtime.processes() == 4);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 4 && f.runtime.processes() == 0);
    for (std::size_t i = 0; i < 4; ++i) f.expect(i, {raw(std::int32_t(i + 1))});
}

void entity_freeze_pauses_remaining_timer() {
    Fixture f;
    const auto entity = f.entity(1);
    f.load(R"wdl(function delayed(tag) { wait(3); emit(tag); })wdl");
    f.start("delayed", {raw(1)}, entity);
    f.start("delayed", {raw(2)});
    f.runtime.tick(64, 1024);
    f.runtime.tick(64, 1024);
    assert(f.events.empty());
    f.runtime.tick(64, 1024);
    assert(f.events.size() == 1 && f.runtime.processes() == 1);
    f.expect(0, {raw(2)});
    f.runtime.tick(64, 0);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 1); // No accumulated frozen time is charged on thaw.
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(1, {raw(1)}, entity);
}

void live_freeze_preserves_null_callback_continuations() {
    for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
        Fixture f;
        f.state.profile = profile;
        const auto entity = f.entity(1);
        f.load(R"wdl(
            var freeze_mode = 1;
            action world_action() { emit(10); }
            function eventLevel() {
                wait(1); emit(100); freeze_mode = 0;
                wait(1); emit(200); freeze_mode = 2;
                wait(1); emit(300);
            }
            function null_tail() { emit(400); }
            function null_control() { wait(1); freeze_mode = 1; emit(500); }
        )wdl");
        f.runtime.enqueue(f.runtime.function("world_action"), {}, entity);
        f.start("eventLevel");
        f.runtime.tick(64, 2048); // The source global, not the stale argument, is authoritative.
        assert(f.events.size() == 1 && f.runtime.processes() == 2);
        f.expect(0, {raw(100)});
        f.runtime.enqueue(f.runtime.function("null_tail"));
        f.runtime.tick(64, 0);
        assert(f.events.size() == 3 && f.runtime.processes() == 2);
        f.expect(1, {raw(10)}, entity);
        f.expect(2, {raw(200)});
        f.runtime.tick(64, 0);
        assert(f.events.size() == 3 && f.runtime.processes() == 2);
        f.state.global("freeze_mode").value = Value::from_int(1);
        f.runtime.tick(64, 2048);
        assert(f.events.size() == 5 && f.runtime.processes() == 0);
        f.expect(3, {raw(300)});
        f.expect(4, {raw(400)});

        f.state.global("freeze_mode").value = Value::from_int(0);
        f.start("null_control");
        f.runtime.enqueue(f.runtime.function("world_action"), {}, entity);
        f.runtime.enqueue(f.runtime.function("null_tail"));
        f.runtime.tick(64, 0); // The resumed NULL callback freezes the later entity, not the NULL tail.
        assert(f.events.size() == 7 && f.runtime.processes() == 1);
        f.expect(5, {raw(500)});
        f.expect(6, {raw(400)});
        f.state.global("freeze_mode").value = Value::from_int(0);
        f.runtime.tick(64, 2048);
        assert(f.events.size() == 8 && f.runtime.processes() == 0);
        f.expect(7, {raw(10)}, entity);
    }
}

void waitt_strict_raw_boundary() {
    Fixture f;
    f.load(R"wdl(function timed(tag,delay) { waitt(delay); emit(tag); })wdl");
    f.start("timed", {raw(1), 128}); // TIME=64: stored countdown is exactly -64.
    f.start("timed", {raw(2), 127}); // Stored -63 is strictly above -TIME.
    f.runtime.tick(64, 0);
    assert(f.events.size() == 1 && f.runtime.processes() == 1);
    f.expect(0, {raw(2)});
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(1, {raw(1)});
}

void continuation_late_order_by_profile() {
    for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
        Fixture f;
        f.state.profile = profile;
        f.load(R"wdl(
            function shared(tag,mark,delay) { if (mark) { proc_late; } wait(delay); emit(tag); }
            function normal(tag,delay) { wait(delay); emit(tag); }
        )wdl");
        f.start("shared", {raw(11), 0, raw(2)});
        f.start("normal", {raw(20), raw(2)});
        f.start("shared", {raw(12), raw(1), raw(2)});
        f.runtime.tick(64, 0);
        assert(f.events.empty() && f.runtime.processes() == 3);
        f.runtime.tick(64, 0); // Retained records keep their original flags.
        assert(f.events.size() == 3 && f.runtime.processes() == 0);
        // A6 reads the live function marker; A7 reads each allocated record's flags.
        f.expect(0, {raw(profile == SemanticProfile::BonusA7 ? 11 : 20)});
        f.expect(1, {raw(profile == SemanticProfile::BonusA7 ? 20 : 11)});
        f.expect(2, {raw(12)});
        f.start("shared", {raw(13), 0, raw(1)});
        f.start("normal", {raw(21), raw(1)});
        f.runtime.tick(64, 0);
        assert(f.events.size() == 5 && f.runtime.processes() == 0);
        f.expect(3, {raw(21)});
        f.expect(4, {raw(13)}); // Future records inherit the function's late marker.
    }
}

void cancellation_targets_suspended_my() {
    for (auto mode : {1, 2}) {
        Fixture f;
        const auto a = f.entity(1), b = f.entity(2), c = f.entity(3);
        f.load(R"wdl(
            function queued(tag) { wait(1); emit(tag); }
            function cancel(mode) { proc_kill(mode); }
        )wdl");
        f.start("queued", {raw(1)}, a, c);
        f.start("queued", {raw(2)}, b, a);
        f.start("queued", {raw(3)}, c, b);
        f.start("queued", {raw(4)}, 0, b);
        f.start("cancel", {raw(mode)}, a, b);
        assert(f.runtime.processes() == 3);
        f.runtime.tick(64, 0);
        assert(f.events.size() == 3 && f.runtime.processes() == 0);
        if (mode == 1) f.expect(0, {raw(2)}, b, a);
        else f.expect(0, {raw(1)}, a, c);
        f.expect(1, {raw(3)}, c, b);
        f.expect(2, {raw(4)}, 0, b);
    }
    Fixture f;
    f.load(R"wdl(
        function same(tag,mode) { if (mode) { proc_kill(mode); } wait(1); emit(tag); }
        function other() { wait(1); emit(2); }
        function suppress() { proc_kill(8); emit(8); wait(1); emit(9); }
    )wdl");
    f.start("same", {raw(1), 0});
    f.start("other");
    f.start("same", {raw(3), raw(4)});
    assert(f.runtime.processes() == 2); // Mode 4 does not abort its active call frame.
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(2)});
    f.expect(1, {raw(3)});
    f.start("suppress");
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(2, {raw(8)});
    f.start("other"); // Mode 8 blocks all continuation insertion until the next tick.
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.start("other");
    assert(f.runtime.processes() == 1); // The tick cleared the global inhibition latch.
    f.runtime.tick(64, 0);
    assert(f.events.size() == 4 && f.runtime.processes() == 0);
    f.expect(3, {raw(2)});
}

void expression_wait_snapshots() {
    Fixture f;
    f.load(R"wdl(
        function snapshot() { var local = 1; wait(1) + (local = 2); emit(local); }
        function twice() { wait(1) + wait(2); emit(7); }
    )wdl");
    f.start("snapshot");
    assert(f.events.empty() && f.runtime.processes() == 1);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(1)}); // Capture occurs at WAIT, before the right-hand assignment.
    f.start("twice");
    assert(f.events.size() == 1 && f.runtime.processes() == 2);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 1);
    f.expect(1, {raw(7)});
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(2, {raw(7)});
}

void null_cancellation_guards() {
    for (auto mode : {1, 2}) {
        Fixture f;
        const auto entity = f.entity(1);
        f.load(R"wdl(
            function queued(tag) { wait(1); emit(tag); }
            function cancel(mode) { proc_kill(mode); }
        )wdl");
        f.start("queued", {raw(1)});
        f.start("queued", {raw(2)}, entity);
        f.start("cancel", {raw(mode)}); // Null MY/YOU must not match global UI records.
        assert(f.runtime.processes() == 2);
        f.runtime.tick(64, 0);
        assert(f.events.size() == 2 && f.runtime.processes() == 0);
        f.expect(0, {raw(1)});
        f.expect(1, {raw(2)}, entity);
    }
}

void synchronous_local_storage_is_reused() {
    Fixture f;
    f.load(R"wdl(function local_call() { var local = 0; local += 1; check(local,1); })wdl");
    f.start("local_call");
    const auto object_count = f.state.objects.size();
    for (int i = 0; i < 100; ++i) f.start("local_call");
    assert(f.state.objects.size() == object_count);
    assert(f.events.empty() && f.runtime.processes() == 0);
}

void yielded_child_retains_parent_reference() {
    Fixture f;
    f.load(R"wdl(
        function child(&value) {
            wait(1);
            value += 5;
            emit(value);
            wait(1);
            value += 7;
            emit(value);
        }
        function parent() { var value = 10; child(value); emit(value); }
        function churn() { var local = 999; check(local,999); }
    )wdl");
    f.start("parent");
    assert(f.events.size() == 1 && f.runtime.processes() == 1);
    f.expect(0, {raw(10)});
    for (int i = 0; i < 100; ++i) f.start("churn");
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 1);
    f.expect(1, {raw(15)});
    for (int i = 0; i < 100; ++i) f.start("churn");
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(2, {raw(22)});
}

void yielded_children_retain_context_reference_slots() {
    for (const auto target : {"my", "you", "me"}) {
        for (const auto cancel_parent : {false, true}) {
            Fixture f;
            const auto a = f.entity(11), b = f.entity(12);
            const auto c = f.entity(21), d = f.entity(22);
            f.load(std::string(R"wdl(
                function child(entity &e) {
                    wait(1); emit(e.skill1); e = null;
                    wait(1); check(e,null); emit(1);
                }
                function main(mode) {
                    if (mode) { proc_kill(4); return; }
                    child()wdl") + target + ");" +
                (cancel_parent ? "wait(2);" : "") + R"wdl(
                }
                function churn() { var local = 999; check(local,999); }
            )wdl");
            f.start("main", {}, a, b);
            f.start("main", {}, c, d);
            if (cancel_parent) f.start("main", {raw(1)});
            assert(f.runtime.processes() == 2);
            for (int i = 0; i < 100; ++i) f.start("churn");
            f.runtime.tick(64, 0);
            assert(f.events.size() == 2 && f.runtime.processes() == 2);
            f.expect(0, {raw(target == std::string_view("you") ? 12 : 11)}, a, b);
            f.expect(1, {raw(target == std::string_view("you") ? 22 : 21)}, c, d);
            for (int i = 0; i < 100; ++i) f.start("churn");
            f.runtime.tick(64, 0);
            assert(f.events.size() == 4 && f.runtime.processes() == 0);
            f.expect(2, {raw(1)}, a, b);
            f.expect(3, {raw(1)}, c, d);
        }
    }
    Fixture f;
    const auto entity = f.entity(1);
    f.load(R"wdl(
        function child(entity &e) { wait(1); e = null; }
        function main() { child(my); }
    )wdl");
    f.start("main", {}, entity);
    f.runtime.tick(64, 0);
    assert(f.runtime.processes() == 0 && f.events.empty());
}

void synchronous_context_reference_rebinding() {
    Fixture f;
    const auto a = f.entity(11), b = f.entity(12);
    f.load(R"wdl(
        function replace(entity &e) { e = you; }
        function main() {
            entity *saved = my;
            replace(me);
            check(my,me);
            emit(my.skill1,me.skill1,you.skill1);
            rebind_context(saved,saved);
            emit(my.skill1,me.skill1,you.skill1);
            wait(1);
            emit(my.skill1,me.skill1,you.skill1);
        }
    )wdl");
    f.start("main", {}, a, b);
    f.expect(0, {raw(12), raw(12), raw(12)}, b, b);
    f.expect(1, {raw(11), raw(11), raw(11)}, a, a);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(2, {raw(11), raw(11), raw(11)}, a, a);
}

void yielded_child_retains_entity_field_payload() {
    Fixture f;
    const auto entity = f.entity(1);
    f.state.member(entity,"x").set(Value::from_int(5));
    f.load(R"wdl(
        function child(var &value) {
            my = null;
            wait(1); emit(value); value += 7;
            wait(1); emit(value);
        }
        function main() { child(my.x); }
    )wdl");
    f.start("main", {}, entity);
    assert(f.events.empty() && f.runtime.processes() == 1);
    f.state.retire(entity);
    assert(f.state.get(entity) && !f.state.require(entity).alive);
    for (int i = 0; i < 100; ++i) f.entity(i);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 1 && f.runtime.processes() == 1);
    f.expect(0, {raw(5)});
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(1, {raw(12)});
    assert(!f.state.get(entity));
}

void bootstrap_and_entity_enqueue_are_deferred() {
    Fixture f;
    const auto entity = f.entity(1);
    f.load(R"wdl(
        function main() { emit(1); }
        starter first() { emit(2); }
        starter second() { emit(3); }
        action entity_action() { emit(4); wait(1); emit(5); }
    )wdl");
    f.runtime.bootstrap();
    assert(f.events.empty() && f.runtime.processes() == 3);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(0, {raw(1)});
    f.expect(1, {raw(3)});
    f.expect(2, {raw(2)});
    f.runtime.enqueue(f.runtime.function("entity_action"), {}, entity);
    assert(f.events.size() == 3 && f.runtime.processes() == 1);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 4 && f.runtime.processes() == 1);
    f.expect(3, {raw(4)}, entity);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 5 && f.runtime.processes() == 0);
    f.expect(4, {raw(5)}, entity);
}

void entity_teardown_preserves_ui_and_clears_you() {
    Fixture f;
    const auto removed = f.entity(1), survivor = f.entity(2);
    f.load(R"wdl(
        function queued(tag) { wait(1); if (you) { emit(99); } emit(tag); }
    )wdl");
    f.start("queued", {raw(1)}, removed, survivor);
    f.start("queued", {raw(2)}, 0, removed);
    f.start("queued", {raw(3)}, survivor, removed);
    f.runtime.kill_for_entity(removed);
    assert(f.runtime.processes() == 2);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(2)});
    f.expect(1, {raw(3)}, survivor);
}

void registered_pointer_cleanup_preserves_copied_values() {
    Fixture f;
    f.load(R"wdl(
        ENTITY* player; PANEL* panel_pointer; VIEW* view_pointer;
        STRING* string_pointer; BMAP* bmap_pointer; MATERIAL* material_pointer;
        ENTITY* other; ENTITY* number_pointer; FUNCTION* callback;
        ENTITY identity; VAR copied; VAR number; VAR values[2];
        FUNCTION camera_guard() { if (player!=null) emit(player.skill1); }
        FUNCTION copied_member() { return(values[0].skill1); }
    )wdl");
    const auto removed = f.entity(7), survivor = f.entity(8);
    f.state.require(removed).field("event_flags").value = Value::from_raw(0x40000000);
    const char* cells[] = {"player", "panel_pointer", "view_pointer", "string_pointer",
                          "bmap_pointer", "material_pointer"};
    for (const auto name : cells) f.state.global(name).value = Value::object(removed);
    f.state.global("other").value = Value::object(survivor);
    f.state.global("number_pointer").value = Value::from_raw(removed);
    f.state.global("callback").value = Value::function(removed);
    f.state.global("identity").value = Value::object(removed);
    f.state.global("copied").value = Value::object(removed);
    f.state.global("number").value = Value::from_raw(removed);
    auto& array = f.state.require(f.state.global("values").value.bits).array;
    array[0].value = Value::object(removed);
    array[1].value = Value::from_raw(removed);
    f.start("camera_guard");
    f.expect(0, {raw(7)});
    assert(f.runtime.start("copied_member").raw() == raw(7));
    assert(f.state.require(removed).retained == 0); // Global copies are not owning leases.
    f.runtime.kill_for_entity(removed);
    for (const auto name : cells) assert(!f.state.global(name).value.bits);
    assert(f.state.global("other").value.bits == survivor);
    assert(f.state.global("number_pointer").value.kind == Value::Kind::Number
           && f.state.global("number_pointer").value.bits == removed);
    assert(f.state.global("callback").value.kind == Value::Kind::Function
           && f.state.global("callback").value.bits == removed);
    for (const auto name : {"identity", "copied", "number"})
        assert(f.state.global(name).value.bits == removed);
    assert(array[0].value.kind == Value::Kind::Object && array[0].value.bits == removed);
    assert(array[1].value.kind == Value::Kind::Number && array[1].value.bits == removed);
    f.state.retire(removed);
    assert(!f.state.get(removed));
    f.start("camera_guard"); // The original outer NULL guard skips invalid member access.
    assert(f.events.size() == 1);
    bool rejected = false;
    // A scalar VAR member selects its numeric-vector proxy; indexed raw copies select the referent.
    try { f.start("copied_member"); } catch (const std::runtime_error& error) {
        assert(std::string_view(error.what()).find("invalid WDL object handle") != std::string_view::npos);
        rejected = true;
    }
    assert(rejected);

    // A replaced program discards its declaration bindings; stale global names aren't registered.
    f.load("VAR player; FUNCTION idle() {}");
    const auto next = f.entity(9);
    f.state.require(next).field("event_flags").value = Value::from_raw(1);
    f.state.global("player").value = Value::object(next);
    f.runtime.kill_for_entity(next);
    assert(f.state.global("player").value.bits == next);
}

void registered_pointer_cleanup_uses_a6_whole_event_mask() {
    for (const auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7})
        for (const std::uint32_t mask : {0u, 1u, 0x40000000u}) {
            Fixture f;
            f.state.profile = profile;
            f.load("ENTITY* pointer; FUNCTION idle() {}");
            const auto id = f.entity(1);
            f.state.require(id).field("event_flags").value = Value::from_raw(mask);
            f.state.require(id).field("action1").value = Value::function(1);
            f.state.global("pointer").value = Value::object(id);
            f.runtime.kill_for_entity(id);
            assert(f.state.global("pointer").value.bits
                   == (profile == SemanticProfile::CampaignA6 && mask ? 0u : id));
        }
    Fixture f;
    f.load("ENTITY* pointer;");
    const auto id = f.state.create(Object::Kind::Particle).id;
    f.state.require(id).field("event_flags").value = Value::from_raw(1);
    f.state.global("pointer").value = Value::object(id);
    f.runtime.kill_for_entity(id);
    assert(f.state.global("pointer").value.bits == id); // Not the entity registry path.
}

void removal_clears_exposed_context_without_dropping_leases() {
    Fixture f;
    f.load(R"wdl(
        ENTITY* player;
        FUNCTION queued() { wait(1); emit(99); }
        FUNCTION survivor() { wait(1); emit(you==null,your==null); }
        FUNCTION field_lease(VAR &value) { my=null; wait(1); emit(value); }
        FUNCTION remove_now() {
            remove_actor(my);
            emit(my==null,me==null,you==null,your==null);
            wait(1); emit(99);
        }
    )wdl");
    const auto removed = f.entity(7);
    f.state.require(removed).field("event_flags").value = Value::from_raw(1);
    f.state.global("player").value = Value::object(removed);
    f.start("queued", {}, removed);
    f.start("survivor", {}, 0, removed);
    const auto ref = f.state.member(removed, "skill1");
    const Argument argument{ref.get(), ref};
    f.runtime.start("field_lease", std::span(&argument, 1), removed);
    f.start("remove_now", {}, removed, removed);
    assert(!f.state.global("player").value.bits);
    f.expect(0, {raw(1), raw(1), raw(1), raw(1)});
    assert(f.runtime.processes() == 2);
    assert(f.state.get(removed) && !f.state.require(removed).alive);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(1, {raw(1), raw(1)});
    f.expect(2, {raw(7)});
    assert(!f.state.get(removed)); // The existing context/reference leases released normally.

    // Also cancel records in the tick's pending queue before they can resume.
    const auto next = f.entity(8);
    f.state.require(next).field("event_flags").value = Value::from_raw(1);
    f.runtime.enqueue(f.runtime.function("remove_now"), {}, next, next);
    f.start("queued", {}, next);
    f.start("survivor", {}, 0, next);
    f.runtime.tick(64, 0);
    assert(f.events.size() == 5 && f.runtime.processes() == 0 && !f.state.get(next));
    f.expect(3, {raw(1), raw(1), raw(1), raw(1)});
    f.expect(4, {raw(1), raw(1)});
}

void native_call_namespaces(Runtime& runtime, State& state) {
    for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
        state.profile = profile;
        state.global("STR_LEN").value = Value::from_int(0);
        runtime.load_text("native-call-namespaces.wdl", R"wdl(
            VAR abs=0;
            VAR initialized=str_len("born");
            VAR before, after, length, parameters, locals, null_result, live_result, member_result, indexed_result;
            FUNCTION* hook;
            VAR table[2]=NULL, script_only;
            ENTITY actor;
            FUNCTION str_len(value) { return 91; }
            FUNCTION abs(value) { return 92; }
            FUNCTION script_only(value) { return value+11; }
            FUNCTION parameter_shadow(str_len,abs) { return str_len("abcd")+abs(-7); }
            FUNCTION local_shadow() { VAR str_len=0; VAR abs=0; return str_len("abc")+abs(-2); }
            FUNCTION indirect(hook) { return hook(3); }
            FUNCTION main() {
                before=STR_LEN; STR_LEN=73; after=STR_LEN;
                length=str_len("native"); parameters=parameter_shadow(NULL,NULL); locals=local_shadow();
                null_result=indirect(NULL); hook=script_only; live_result=hook(3);
                actor.event=script_only; member_result=actor.event(4);
                indexed_result=table[0](8)+table[1](5);
            }
        )wdl");
        runtime.start("main");
        for (auto [name, expected] : {std::pair{"before",0}, {"after",73}, {"length",6},
                                     {"parameters",11}, {"locals",5}, {"null_result",0},
                                     {"live_result",14}, {"member_result",15}, {"indexed_result",16},
                                     {"initialized",4}})
            assert(state.global(name).value.raw() == raw(expected));
        assert(state.global("abs").value.raw() == 0);
        assert(runtime.diagnostics().empty() && runtime.processes() == 0);

        // Typed NULL cells still read as NULL; native NAME(...) is a separate compiler namespace.
        runtime.load_text("native-call-namespaces.wdl", R"wdl(
            FUNCTION* str_len;
            VAR is_null, length;
            FUNCTION main() { is_null=(str_len==NULL); length=str_len("abc"); }
        )wdl");
        runtime.start("main");
        assert(state.global("is_null").value.raw() == raw(1));
        assert(state.global("length").value.raw() == raw(3));
        runtime.load_text("native-call-namespaces.wdl", R"wdl(
            VAR length;
            FUNCTION main() { length=str_len("reload"); }
            FUNCTION unsupported() { cd_track(); }
            FUNCTION xtest() { return 17; }
            FUNCTION atan2() { return 19; }
            FUNCTION campaign_only() { return xtest(); }
            FUNCTION bonus_only() { return atan2(); }
        )wdl");
        runtime.start("main");
        assert(state.global("length").value.raw() == raw(6));
        const bool bonus = profile == SemanticProfile::BonusA7;
        assert(runtime.start(bonus ? "campaign_only" : "bonus_only").raw() == raw(bonus ? 17 : 19));
        for (auto name : {"unsupported", bonus ? "bonus_only" : "campaign_only"}) {
            bool rejected = false;
            try { runtime.start(name); }
            catch (const std::runtime_error& error) {
                const std::string_view message(error.what());
                assert(message.find("native-call-namespaces.wdl:") != std::string_view::npos);
                assert(message.find(name == std::string_view("unsupported") ? "cd_track" : bonus ? "atan2" : "xtest") != std::string_view::npos);
                rejected = true;
            }
            assert(rejected && runtime.processes() == 0);
        }
    }
}

void engine_birth_and_load_use_registered_pointer_cleanup() {
    // Engine creates a save directory at construction; never use the user's save namespace.
    struct PrivateEnvironment {
        std::filesystem::path root;
        std::optional<std::string> previous;
        PrivateEnvironment() {
            auto pattern = (std::filesystem::temp_directory_path() / "k22-pointer-XXXXXX").string();
            const auto* directory = ::mkdtemp(pattern.data());
            if (!directory) throw std::system_error(errno, std::generic_category(), "mkdtemp");
            root = directory;
            if (const auto* value = std::getenv("XDG_DATA_HOME")) previous = value;
            if (::setenv("XDG_DATA_HOME", root.c_str(), 1))
                throw std::system_error(errno, std::generic_category(), "setenv");
        }
        ~PrivateEnvironment() {
            if (previous) ::setenv("XDG_DATA_HOME", previous->c_str(), 1);
            else ::unsetenv("XDG_DATA_HOME");
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } temporary;
    Assets assets(temporary.root);
    Media media(assets, false);
    State state;
    Engine engine(assets, nullptr, media, state);
    Runtime runtime(state, [&engine](Runtime& vm, ProcessContext& context,
                                   std::string_view name, std::span<const Argument> args) {
        return engine.builtin(vm, context, name, args);
    });
    runtime.load_text("pointer-lifecycle.wdl", R"wdl(
        ENTITY* player; ENTITY* born; VAR initial_mask;
        ACTION publish() { initial_mask=my.event_flags; born=my; }
        FUNCTION camera_guard() { if (player!=null) player.skill1=42; }
        FUNCTION remove_me() {
            ent_remove(me);
            if (my!=null || me!=null || you!=null || your!=null) error("context not NULL");
        }
    )wdl");
    const auto actor = engine.create_entity(runtime, "", {}, runtime.function("publish"));
    assert(state.global("initial_mask").value.bits == 1u); // Bit1 exists before optional action.
    assert(state.global("born").value.bits == actor);
    state.global("player").value = Value::object(actor);
    runtime.start("camera_guard");
    assert(state.member(actor, "skill1").get().raw() == raw(42));
    engine.load_level(runtime, "");
    assert(!state.global("player").value.bits && !state.global("born").value.bits);
    assert(!state.get(actor));
    runtime.start("camera_guard"); // No replacement publication yet; unchanged source guard is enough.

    const auto timer = engine.create_entity(runtime, "", {}, 0);
    assert(state.require(timer).field("event_flags").value.bits == 1u);
    state.global("player").value = Value::object(timer);
    runtime.start("remove_me", {}, timer, timer);
    assert(!state.global("player").value.bits && !state.get(timer));
    native_call_namespaces(runtime, state);
}

void lazy_compound_property_tracks_pointer_rebinding() {
    Fixture f;
    f.load(R"wdl(
        entity* player;
        panel hidden { digits = 0,0,4,null,1,player.skill27; }
    )wdl");
    f.runtime.tick(1024, 0); // A hidden initializer must not eagerly dereference NULL.
    auto& panel = f.state.require(f.state.global("hidden").value.bits);
    assert(panel.properties.size() == 1);
    auto& property = panel.properties.front();
    assert(property.name == "digits" && property.arguments.size() == 6);
    auto& live_value = property.arguments[5];
    bool rejected_null = false;
    try {
        (void)live_value.current();
    } catch (const std::runtime_error& error) {
        const std::string_view message(error.what());
        assert(message.find("wdl_regression.wdl:") != std::string_view::npos);
        assert(message.find("null") != std::string_view::npos);
        rejected_null = true;
    }
    assert(rejected_null); // NULL is not silently converted into a fictitious skill value.
    const auto first = f.state.create(Object::Kind::Entity).id;
    f.state.member(first, "skill27").set(Value::from_int(17));
    f.state.global("player").value = Value::object(first);
    assert(live_value.current().raw() == raw(17));
    const auto second = f.state.create(Object::Kind::Entity).id;
    f.state.member(second, "skill27").set(Value::from_int(23));
    f.state.global("player").value = Value::object(second);
    assert(live_value.current().raw() == raw(23));
    f.state.member(second, "skill27").set(Value::from_int(29));
    assert(live_value.current().raw() == raw(29));
    assert(f.events.empty() && f.runtime.processes() == 0);
}

void declared_dll_function_pointer_dispatch() {
    Fixture f;
    f.load(R"wdl(
        dllfunction emit;
        Function* hook = emit;
        function main() { hook(81); }
    )wdl");
    f.start("main");
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(81)});
}

void digit_leading_identifiers_and_member_aliases() {
    Fixture f;
    f.load(R"wdl(
        DEFINE 1point_x,SKILL2;
        DEFINE 3rdMesafe,SKILL1;
        IFDEF 3rdMesafe;
        VAR enabled = 1;
        ENDIF;
        ENTITY* focus;
        VAR 1abc = 9;
        function consumer() { emit(focus.1point_x,focus.3rdMesafe,enabled); }
        function 3consumer() { emit(1abc); }
    )wdl");
    const auto entity = f.state.create(Object::Kind::Entity).id;
    f.state.member(entity, "skill1").set(Value::from_int(47));
    f.state.member(entity, "skill2").set(Value::from_int(25));
    f.state.global("focus").value = Value::object(entity);
    f.start("consumer");
    f.start("3consumer");
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(25), raw(47), raw(1)});
    f.expect(1, {raw(9)});
}

void malformed_numeric_literal_is_rejected() {
    Fixture f;
    bool rejected = false;
    try {
        f.load("var malformed = 1.2.3;");
    } catch (const std::runtime_error& error) {
        const std::string_view message(error.what());
        assert(message.find("wdl_regression.wdl:") != std::string_view::npos);
        assert(message.find("invalid numeric") != std::string_view::npos);
        rejected = true;
    }
    assert(rejected);
}

void legacy_resource_comma_constructor() {
    Fixture f;
    f.load("BMAP blood22_,<korublood.bmp>;");
    const auto value = f.state.global("blood22_").value;
    assert(value.kind == Value::Kind::Object);
    const auto& bmap = f.state.require(value.bits);
    assert(bmap.kind == Object::Kind::Bmap && bmap.text == "korublood.bmp");
    assert(f.events.empty() && f.runtime.processes() == 0);
}

void parenthesized_array_declaration_initializes_storage() {
    Fixture f;
    f.load(R"wdl(
        var map_sizes[17]
        (
            1,0,0,0,1,0,0,1,1,1,1,1,1,0,1,1,1;
        );
        var marker = 23;
        var_nsave offsets[3] = (2,-3;);
        var ordinary[2] = (4,5);
        function main() {
            var i = 0;
            var total = 0;
            while (i < 17) { total += map_sizes[i]; i += 1; }
            emit(total,map_sizes[16],marker);
            map_sizes[13] = 7;
            emit(map_sizes[13],map_sizes[14]);
            check(offsets[0],2); check(offsets[1],-3); check(offsets[2],0);
            check(ordinary[0],4); check(ordinary[1],5);
        }
    )wdl");
    const auto& array = f.state.require(f.state.global("map_sizes").value.bits);
    assert(array.array.size() == 17);
    f.start("main");
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(11), raw(1), raw(23)});
    f.expect(1, {raw(7), raw(1)});
    Fixture invalid;
    bool rejected = false;
    try {
        invalid.load("function main() { emit((1;)); }");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected); // Declaration punctuation does not relax executable expressions.
}

void statement_dispatch_skips_leading_commas() {
    Fixture f;
    f.load(R"wdl(
        var first = 1;, var second = 2;
        function main() {
            var initialized = 0;,
            var coordinates[3];
            initialized += first + second;
            coordinates[1] = 7;
            if (0) { ,emit(99);, }
            if (1) { ,emit(initialized,coordinates[1],coordinates[2]);, }
        }
    )wdl");
    f.start("main");
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(3), raw(7), raw(0)});
}

void numeric_array_initializer_ignores_unpaired_parenthesis() {
    Fixture f;
    f.load(R"wdl(
        var entry[4] = 5708.59,633.3,478.03,270);
        var next = 9;
        function main() {
            emit(entry[0],entry[1],entry[2],entry[3],next);
            entry[3] += 1;
            emit(entry[3]);
        }
    )wdl");
    f.start("main");
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {5845596, 648499, 489502, raw(270), raw(9)});
    f.expect(1, {raw(271)});
}

void implicit_declaration_initializers_and_callback_bindings() {
    Fixture f;
    f.state.global("on_esc").value = Value::from_int(99);
    for (auto name : {"on_esc", "on_space", "render_world"}) f.state.global(name).callback = true;
    f.load(R"wdl(
        var positions[3]
        -7,+8,-9;
        var_nsave separated[4] -1 +2 -3;
        var signed_scalar -5;
        ON_ESC NULL;
        ON_SPACE escape;
        RENDER_WORLD = escape;
        function escape() {
            check(separated[0],-1); check(separated[1],2);
            check(separated[2],-3); check(separated[3],0);
            emit(positions[0],positions[1],positions[2],signed_scalar);
        }
        function main() { check(on_esc,null); on_space(); render_world(); }
    )wdl");
    assert(f.events.empty()); // Binding a function must not invoke it during load.
    f.start("main");
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(-7), raw(8), raw(-9), raw(-5)});
    f.expect(1, {raw(-7), raw(8), raw(-9), raw(-5)});
    Fixture invalid;
    invalid.state.global("on_made_up");
    bool rejected = false;
    try {
        invalid.load("function escape() {} ON_MADE_UP escape;");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected); // A name or ON_* prefix is not a callback catalogue entry.
}

void text_string_property_reads_whitespace_separated_names() {
    Fixture f;
    f.load(R"wdl(
        string line0 = "zero"; string line1 = "one";
        string line2 = "two"; string line3 = "three";
        string line4 = "four"; string line5 = "five";
        string line6 = "six"; string line7 = "seven";
        text caption {
            strings = 8;
            string = line0,line1,line2,line3,line4,line5
                line6,line7;
        }
        function main() {
            caption.index = 7; check(caption.string,line6);
            caption.index = 8; check(caption.string,line7);
            emit(caption.index);
        }
    )wdl");
    const auto& caption = f.state.require(f.state.global("caption").value.bits);
    assert(caption.array.size() == 8);
    const char* names[] = {"line0", "line1", "line2", "line3",
                           "line4", "line5", "line6", "line7"};
    for (std::size_t i = 0; i < caption.array.size(); ++i) {
        const auto actual = caption.array[i].value, expected = f.state.global(names[i]).value;
        assert(actual.kind == expected.kind && actual.bits == expected.bits);
    }
    f.start("main");
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(8)});
    Fixture invalid;
    bool rejected = false;
    try {
        invalid.load("function main() { emit(1)\nemit(2); }");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected); // A newline still cannot replace an executable statement's semicolon.
}

void material_birth_defaults_reach_script_consumers() {
    for (auto profile : {SemanticProfile::CampaignA6, SemanticProfile::BonusA7}) {
        Fixture f;
        f.state.profile = profile;
        f.load(R"wdl(
            MATERIAL ordinary {};
            MATERIAL edited { ambient_red=17; diffuse_blue=0; alpha=0; albedo=0; cycle=3; }
            MATERIAL *born;
            function defaults(m) {
                emit(m.ambient_blue,m.ambient_green,m.ambient_red);
                emit(m.diffuse_blue,m.diffuse_green,m.diffuse_red);
                emit(m.map_blue,m.map_green,m.map_red);
                emit(m.alpha,m.albedo,m.cycle,m.power,m.scale1,m.scale2);
                emit(m.specular_blue,m.specular_green,m.specular_red,
                     m.emissive_blue,m.emissive_green,m.emissive_red,m.skill1,m.skill20);
            }
            function consumer() {
                defaults(ordinary);
                emit(edited.ambient_red,edited.diffuse_blue,edited.alpha,edited.albedo,edited.cycle);
                defaults(born);
            }
        )wdl");
        const auto retired = f.state.create(Object::Kind::Material).id;
        f.state.member(retired, "ambient_red").set(Value::from_int(-17));
        f.state.member(retired, "albedo").set(Value::from_int(-3));
        f.state.member(retired, "skill1").set(Value::from_int(9));
        f.state.retire(retired);
        f.state.global("born").value = Value::object(f.state.create(Object::Kind::Material).id);
        f.start("consumer");
        assert(f.events.size() == 11 && f.runtime.processes() == 0);
        for (auto offset : {0u, 6u}) {
            for (auto channel = 0u; channel < 3; ++channel)
                f.expect(offset + channel, {raw(255), raw(255), raw(255)});
            f.expect(offset + 3, {raw(100), 0xc8df, raw(480), 0, 0, 0});
            f.expect(offset + 4, {0, 0, 0, 0, 0, 0, 0, 0});
        }
        f.expect(5, {raw(17), 0, 0, 0, raw(3)});
    }
}

void native_expression_compaction_and_compiler_metadata() {
    Fixture f;
    f.load(R"wdl(
        PLUGINDIR = ".";
        BIND <korublood.bmp>;
        LEVEL <map.wmb>;
        material Mtl_arkakum {};
        STRING phrase = "two words";
        function main() {
            Mtl_ arkakum.ambient_ red = 172;
            check(Mtl_ arkakum.ambient_red,172);
            emit(1 2);
        }
    )wdl");
    const auto& settings = f.runtime.compiler_settings();
    assert(settings.plugin_directory == ".");
    assert(settings.bindings == std::vector<std::string>{"korublood.bmp"});
    assert(settings.levels == std::vector<std::string>{"map.wmb"});
    assert(f.runtime.metrics().directives == 3);
    f.start("main");
    const auto material = f.state.global("Mtl_arkakum").value.bits;
    assert(f.state.member(material, "ambient_red").get().raw() == raw(172));
    const auto phrase = f.state.global("phrase").value.bits;
    assert(f.state.require(phrase).text == "two words");
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(12)});
}

void split_operator_expression_compaction() {
    Fixture f;
    const auto entity = f.entity(7);
    f.load(R"wdl(
        var kursun_hizi = 100;
        var time_scale = 0.5;
        var other = 2;
        var vals[3] = (1 2 3);
        STRING phrase = "two words";
        Entity* focus;
        function main() {
            kursun_hizi*		=time_scale;
            check(kursun_hizi,50);
            other + = 2; check(other,4);
            other - = 2; check(other,2);
            other / = 2; check(other,1);
            other = 7;
            other % = 2; check(other,1);
            other = 7;
            other & = 3; check(other,3);
            other | = 4; check(other,7);
            other ^ = 2; check(other,5);
            check(2 = = 2,1);
            check(2 ! = 3,1);
            check(2 < = 2,1);
            check(2 > = 2,1);
            check(1 & & 1,1);
            check(0 | | 1,1);
            check(2 < < 1,4);
            check(4 > > 1,2);
            other = 2;
            check(+ + other,3); check(other,3);
            check(other + +,3); check(other,4);
            check(- - other,3); check(other,3);
            check(other - -,3); check(other,2);
            check(focus - > skill1,7);
            check(vals[0],1); check(vals[1],2); check(vals[2],3);
            emit(kursun_hizi,other);
        }
    )wdl");
    f.state.global("focus").value = Value::object(entity);
    f.start("main");
    const auto phrase = f.state.global("phrase").value.bits;
    assert(f.state.require(phrase).text == "two words");
    assert(f.events.size() == 1 && f.runtime.processes() == 0);
    f.expect(0, {raw(50), raw(2)});
}

void yielded_child_retains_parameter_and_builtin_argument() {
    Fixture f;
    f.load(R"wdl(
        function child(&value) {
            wait(1); emit(value); value += 7; wait(1); emit(value);
        }
        function value_parent(value) { child(value); }
        function argument_parent() { child(_arg1); }
        function churn() { var local = 999; check(local,999); }
    )wdl");
    f.start("value_parent", {raw(5)});
    assert(f.events.empty() && f.runtime.processes() == 1);
    for (int i = 0; i < 100; ++i) f.start("churn");
    f.runtime.tick(64, 0);
    assert(f.events.size() == 1 && f.runtime.processes() == 1);
    f.expect(0, {raw(5)});
    for (int i = 0; i < 100; ++i) f.start("churn");
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(1, {raw(12)});
    f.start("argument_parent", {raw(9)});
    for (int i = 0; i < 100; ++i) f.start("churn");
    f.runtime.tick(64, 0);
    assert(f.events.size() == 3 && f.runtime.processes() == 1);
    f.expect(2, {raw(9)});
    for (int i = 0; i < 100; ++i) f.start("churn");
    f.runtime.tick(64, 0);
    assert(f.events.size() == 4 && f.runtime.processes() == 0);
    f.expect(3, {raw(16)});
}

void object_parameter_members_and_arrays() {
    Fixture f;
    const auto entity = f.state.create(Object::Kind::Entity).id;
    f.state.member(entity, "skill27").set(Value::from_int(17));
    auto& vector = f.state.create(Object::Kind::Vector);
    for (auto value : {2, 3, 4}) vector.array.push_back({Value::from_int(value)});
    std::vector<ObjectId> strings;
    for (const auto word : {"first", "second", "third"}) {
        auto& string = f.state.create(Object::Kind::String);
        string.text = word;
        strings.push_back(string.id);
    }
    auto& text = f.state.create(Object::Kind::Text);
    for (auto id : strings) text.array.push_back({Value::object(id)});
    f.state.member(text.id, "index").set(Value::from_int(3));
    f.load(R"wdl(
        function read_entity(e) { emit(e.skill27); }
        function read_array(a) { emit(a[1],a.y); }
        function read_text(t) { return(t.string[1]); }
        function bad_numeric(value) { emit(value.unknownmember); }
    )wdl");
    std::vector<Argument> args{{Value::object(entity), {}}};
    f.runtime.start("read_entity", args);
    args[0].value = Value::object(vector.id);
    f.runtime.start("read_array", args);
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(17)});
    f.expect(1, {raw(3), raw(3)});
    args[0].value = Value::object(text.id);
    const auto returned_string = f.runtime.start("read_text", args);
    assert(returned_string.kind == Value::Kind::Object && returned_string.bits == strings[1]);
    bool rejected = false;
    try {
        f.start("bad_numeric", {raw(5)});
    } catch (const std::runtime_error& error) {
        const std::string_view message(error.what());
        assert(message.find("wdl_regression.wdl:") != std::string_view::npos);
        assert(message.find("member") != std::string_view::npos);
        rejected = true;
    }
    assert(rejected && f.events.size() == 2);
}

void call_arguments_are_captured_right_to_left() {
    Fixture f;
    f.load(R"wdl(
        var i = 0;
        function pair(a,b) { emit(a,b); }
        function main() { pair(i=1,i=2); emit(i++,i++); }
    )wdl");
    f.start("main");
    assert(f.events.size() == 2 && f.runtime.processes() == 0);
    f.expect(0, {raw(1), raw(2)});
    f.expect(1, {raw(2), raw(1)});
    assert(f.state.global("i").value.raw() == raw(3));
}

void resolved_null_callbacks_return_zero_after_arguments() {
    Fixture f;
    f.load(R"wdl(
        FUNCTION* hook;
        VAR table[2] = NULL, enter;
        VAR side_effect = 0;
        FUNCTION accessibility;
        FUNCTION call_shadow(accessibility) { return accessibility(7); }
        FUNCTION main() {
            hook = accessibility;
            check(hook(0),0);
            hook = table[0];
            check(hook(side_effect=1,side_effect=2),0);
            check(side_effect,1);
            check(table[0](side_effect+=1),0);
            check(call_shadow(NULL),0);
            hook = table[1];
            hook(side_effect);
            forward();
        }
        FUNCTION accessibility(old_level) { emit(10,old_level); return 0; }
        FUNCTION enter(old_level) { emit(20,old_level); }
        FUNCTION forward() { emit(30); }
        FUNCTION invalid() { hook=3; hook(); }
        FUNCTION unknown_native() { missing_native(); }
    )wdl");
    f.start("main");
    assert(f.events.size() == 3 && f.runtime.processes() == 0);
    f.expect(0, {raw(10),0});
    f.expect(1, {raw(20),raw(2)});
    f.expect(2, {raw(30)});
    for (auto name : {"invalid", "unknown_native"}) {
        bool rejected = false;
        try { f.start(name); }
        catch (const std::runtime_error& error) {
            assert(std::string_view(error.what()).find("wdl_regression.wdl:") != std::string_view::npos);
            rejected = true;
        }
        assert(rejected);
    }
}

void panel_digits_numeric_binding_warning_policy() {
    Fixture f;
    f.state.global("warn_level").value = Value::from_int(1);
    f.load(R"wdl(
        VAR live_counter = 7;
        STRING wrong_type = "wrong";
        FONT debug_font = "Arial", 1, 14;
        PANEL debug_panel {
            DIGITS = 0,0,"JOY_BUTTONS - %.0f",debug_font,1,JOY_BUTTONS;
            DIGITS = 0,14,"loc_sik - %.0f",debug_font,1,loc_sik;
            DIGITS = 0,28,"live_counter - %.0f",debug_font,1,live_counter;
            DIGITS = 0,42,"wrong_type - %.0f",debug_font,1,wrong_type;
            DIGITS = 0,56,"JOY2_BUTTONS - %.0f",debug_font,1,JOY2_BUTTONS;
            DIGITS = 0,70,"loc_oldmap - %.0f",debug_font,1,loc_oldmap;
        }
        FUNCTION main() { live_counter = 19; }
    )wdl");
    assert(f.state.globals.find("JOY_BUTTONS") == f.state.globals.end());
    assert(f.state.globals.find("loc_sik") == f.state.globals.end());
    assert(!f.state.globals.contains("JOY2_BUTTONS") && !f.state.globals.contains("loc_oldmap"));
    auto& panel = f.state.require(f.state.global("debug_panel").value.bits);
    assert(panel.properties.size() == 6);
    const std::string_view formats[] = {
        "JOY_BUTTONS - %.0f", "loc_sik - %.0f",
        "live_counter - %.0f", "wrong_type - %.0f",
        "JOY2_BUTTONS - %.0f", "loc_oldmap - %.0f"
    };
    assert(panel.properties[0].arguments.size() == 6);
    const auto* fallback = panel.properties[0].arguments[5].ref.slot;
    assert(fallback);
    for (std::size_t i = 0; i < panel.properties.size(); ++i) {
        const auto& property = panel.properties[i];
        assert(property.name == "digits" && property.arguments.size() == 6);
        const auto format = property.arguments[2].current();
        assert(format.kind == Value::Kind::Object);
        assert(f.state.require(format.bits).text == formats[i]);
        if (i == 2) continue;
        const auto& target = property.arguments[5];
        assert(target.current().kind == Value::Kind::Number && target.current().raw() == 0);
        assert(target.ref.slot == fallback);
        assert(target.ref.owner == 0 && target.ref.field.empty() && target.ref.bitMask == 0);
    }
    for (const auto& [name, slot] : f.state.globals) {
        (void)name;
        assert(&slot != fallback);
    }
    const auto& live = panel.properties[2].arguments[5];
    assert(live.ref.slot == &f.state.global("live_counter"));
    assert(live.current().raw() == raw(7));
    f.start("main");
    assert(live.current().kind == Value::Kind::Number && live.current().raw() == raw(19));
    panel.properties[0].arguments[5].ref.set(Value::from_int(31));
    for (auto i : {0u,1u,3u,4u,5u})
        assert(panel.properties[i].arguments[5].current().raw() == raw(31));

    const auto rejects = [](int warning_level, std::string_view source,
                            std::string_view unresolved) {
        Fixture strict;
        strict.state.global("warn_level").value = Value::from_int(warning_level);
        bool rejected = false;
        try {
            strict.runtime.load_text("wdl_regression.wdl", source);
        } catch (const std::runtime_error& error) {
            const std::string_view message(error.what());
            assert(message.find("wdl_regression.wdl:") != std::string_view::npos);
            assert(message.find(unresolved) != std::string_view::npos);
            rejected = true;
        }
        assert(rejected);
    };
    rejects(2, R"wdl(
        FONT debug_font = "Arial", 1, 14;
        PANEL debug_panel { DIGITS = 0,0,"loc_sik - %.0f",debug_font,1,loc_sik; }
    )wdl", "loc_sik");
    rejects(2, R"wdl(
        STRING wrong_type = "wrong";
        FONT debug_font = "Arial", 1, 14;
        PANEL debug_panel { DIGITS = 0,0,"wrong_type - %.0f",debug_font,1,wrong_type; }
    )wdl", "wrong_type");
    rejects(2, R"wdl(
        STRING* wrong_pointer;
        PANEL debug_panel { DIGITS = 0,0,4,NULL,1,wrong_pointer; }
    )wdl", "wrong_pointer");
    rejects(2, R"wdl(
        BMAP track = <track.tga>;
        PANEL controls { HSLIDER = 0,0,100,track,0,100,missing_slider; }
    )wdl", "missing_slider");
    rejects(2, R"wdl(
        BMAP track = <track.tga>;
        PANEL controls { WINDOW = 0,0,20,20,track,missing_x,missing_y; }
    )wdl", "missing_x");
    rejects(1, R"wdl(
        PANEL debug_panel { DIGITS = 0,0,4,NULL,1,unknown_expression+1; }
    )wdl", "unknown_expression");
    rejects(1, "FUNCTION main() { emit(undeclared); }", "undeclared");
    rejects(1, R"wdl(
        VAR live_counter = 7;
        PANEL debug_panel { DIGITS = 0,0,"%.0f",unknown_font,1,live_counter; }
    )wdl", "unknown_font");
    rejects(1, R"wdl(
        VAR live_counter = 7;
        FONT debug_font = "Arial", 1, 14;
        PANEL debug_panel { DIGITS = unknown_x,0,"%.0f",debug_font,1,live_counter; }
    )wdl", "unknown_x");
}

void numeric_control_bindings_preserve_references_and_flags() {
    Fixture f;
    f.state.global("warn_level").value = Value::from_int(2);
    f.load(R"wdl(
        VAR counter = 7;
        VAR numbers[2] = 3,4;
        PANEL subject { VISIBLE = ON; }
        BMAP track = <track.tga>;
        FONT face = "Arial",1,14;
        PANEL controls {
            DIGITS = 0,0,"%.0f",face,1,numbers[1];
            DIGITS = 0,0,"%.0f",face,1,counter.y;
            DIGITS = 0,0,"%.0f",face,1,subject.visible;
            HSLIDER = 0,0,100,track,0,100,counter;
            WINDOW = 0,0,20,20,track,numbers[1],counter.y;
        }
        FUNCTION main() { numbers[1]=9; counter.y=11; counter=13; subject.visible=OFF; }
    )wdl");
    auto& properties = f.state.require(f.state.global("controls").value.bits).properties;
    assert(properties.size() == 5);
    auto& array = properties[0].arguments[5];
    auto& member = properties[1].arguments[5];
    auto& flag = properties[2].arguments[5];
    assert(array.ref.slot && array.ref.index == 1);
    assert(member.ref.slot && member.ref.field == "y");
    assert(flag.ref.slot && flag.ref.bitMask == 0x4000u);
    assert(properties[3].arguments[6].ref.slot == &f.state.global("counter"));
    assert(properties[4].arguments[5].ref.slot == array.ref.slot);
    assert(properties[4].arguments[6].ref.slot == member.ref.slot);
    f.start("main");
    assert(array.current().raw() == raw(9) && member.current().raw() == raw(11));
    assert(flag.current().raw() == 0 && properties[3].arguments[6].current().raw() == raw(13));
    flag.ref.set(Value::from_int(1));
    assert(flag.current().raw() == raw(1));

    f.state.global("warn_level").value = Value::from_int(1);
    f.load(R"wdl(
        BMAP track = <track.tga>;
        PANEL controls {
            HSLIDER = 0,0,100,track,0,100,missing_slider;
            WINDOW = 0,0,20,20,track,missing_x,missing_y;
        }
    )wdl");
    auto& fallback = f.state.require(f.state.global("controls").value.bits).properties;
    const auto& slider = fallback[0].arguments[6];
    const auto& x = fallback[1].arguments[5];
    const auto& y = fallback[1].arguments[6];
    assert(slider.ref.slot == &f.state.numericBindingFallback);
    assert(x.ref.slot == slider.ref.slot && y.ref.slot == slider.ref.slot);
    slider.ref.set(Value::from_int(17));
    assert(x.current().raw() == raw(17) && y.current().raw() == raw(17));
    assert(!f.state.globals.contains("missing_slider") && !f.state.globals.contains("missing_x")
           && !f.state.globals.contains("missing_y"));
    const auto persistent = slider.ref;
    f.load("PANEL reader { DIGITS=0,0,4,NULL,1,another_unknown; }");
    const auto& reloaded = f.state.require(f.state.global("reader").value.bits).properties[0].arguments[5];
    assert(reloaded.ref.slot == persistent.slot && reloaded.current().raw() == raw(17));
    assert(persistent.get().raw() == raw(17));
}

void obsolete_window_is_parsed_without_finalizing() {
    Fixture f;
    f.runtime.load_text("wdl_regression.wdl", R"wdl(
        WINDOW WINSTART { BG_COLOR RGB(0,0,0); MODE IMAGE; };
    )wdl");
    f.runtime.tick(64, 0); // Evaluating RGB would trigger the fixture's unknown-builtin error.
    assert(f.state.globals.find("WINSTART") == f.state.globals.end());
    bool native_diagnostic = false;
    for (const auto& diagnostic : f.runtime.diagnostics())
        if (diagnostic.find("00419c20") != std::string::npos) native_diagnostic = true;
    assert(native_diagnostic);
    assert(f.events.empty() && f.runtime.processes() == 0);
}
// Coop banks (docs/COOP.md): per-process global banks swapped on run() entry/exit; bank 0 live between processes.
void coop_banks_isolate_processes() {
    Fixture f;
    f.load(R"wdl(
        var score; ENTITY* hero; var ammo[2]; VECTOR dist; STRING label = "a"; var shared;
        function worker(n) { score = n; hero = my; ammo[0] = n*2; dist.x = n; set_text(label, n); shared += 1;
            while (1) { wait(1); emit(n, score, ammo[0], dist.x, hero == my); } }
        function peek() { return score; }
        function outer() { score = 9; var got = peek(); emit(got, score); }
        function skipme() { shared = 100; return 5; }
        function roamer() { while (1) { emit(score); wait(1); } }
    )wdl");
    const auto e0 = f.entity(0), e1 = f.entity(1);
    f.state.require(e1).field("event_flags").value = Value::from_raw(1);
    int dynamicBank = 0;
    std::vector<std::pair<std::string, int>> asked;
    const std::vector<std::string> names{"score", "hero", "ammo", "dist", "label"};
    f.runtime.enable_banks(names, [&](FunctionId fn, ObjectId my, int inherited) {
        const std::string name(f.runtime.function_name(fn));
        asked.emplace_back(name, inherited);
        if (name == "worker") return my == e1 ? 1 : 0;
        if (name == "peek") return 0;
        if (name == "skipme") return Runtime::BankSkip;
        if (name == "roamer") return inherited == Runtime::BankDynamic ? dynamicBank : Runtime::BankDynamic;
        return my == e1 ? 1 : inherited;
    });
    assert(f.runtime.function_file(f.runtime.function("worker")) == "wdl_regression.wdl");
    auto& ammo = f.state.require(f.state.global("ammo").value.bits).array;
    auto& dist = f.state.require(f.state.global("dist").value.bits).array;
    auto& label = f.state.require(f.state.global("label").value.bits).text;
    const Slot* ammo0 = &ammo[0];
    f.start("worker", {raw(1)}, e0);
    f.start("worker", {raw(2)}, e1);
    assert(f.runtime.active_bank() == 0);
    assert(asked.size() == 2 && asked[0].second == 0 && asked[1].second == 0);
    // Bank 0 live between processes; bank 1 stored. Element storage never moves.
    assert(f.state.global("score").value.raw() == raw(1) && f.runtime.bank_value(1, "score").raw() == raw(2));
    assert(f.runtime.bank_value(0, "score").raw() == raw(1));
    assert(f.state.global("hero").value.bits == e0 && f.runtime.bank_value(1, "hero").bits == e1);
    assert(&ammo[0] == ammo0 && ammo[0].value.raw() == raw(2) && dist[0].value.raw() == raw(1) && label == "1");
    assert(f.state.global("shared").value.raw() == raw(2)); // unbanked: shared by both
    f.runtime.tick(64, 0);
    assert(f.events.size() == 2 && f.runtime.active_bank() == 0);
    f.expect(0, {raw(1), raw(1), raw(2), raw(1), raw(1)}, e0);
    f.expect(1, {raw(2), raw(2), raw(4), raw(2), raw(1)}, e1);
    assert(&ammo[0] == ammo0 && label == "1");

    // Nested call from bank 1 into a bank-0 function sees bank 0 and restores bank 1 on return.
    f.events.clear();
    asked.clear();
    f.start("outer", {}, e1);
    f.expect(0, {raw(1), raw(9)}, e1);
    assert(asked.size() == 2 && asked[1] == std::make_pair(std::string("peek"), 1));
    assert(f.state.global("score").value.raw() == raw(1) && f.runtime.bank_value(1, "score").raw() == raw(9));
    f.runtime.set_bank_value(1, "score", Value::from_int(2));
    assert(f.runtime.bank_value(1, "score").raw() == raw(2));

    // BankSkip: start returns 0 without running; enqueue drops it.
    const auto processes = f.runtime.processes();
    assert(!f.runtime.start("skipme") && f.state.global("shared").value.raw() == raw(2));
    f.runtime.enqueue(f.runtime.function("skipme"));
    assert(f.runtime.processes() == processes);

    // BankDynamic is re-resolved at every resume.
    f.runtime.enqueue(f.runtime.function("roamer"));
    auto roamer = [&] {
        for (auto it = f.events.rbegin(); it != f.events.rend(); ++it)
            if (!it->context.my) return it->values.at(0);
        assert(false);
        return 0;
    };
    f.events.clear();
    dynamicBank = 1;
    f.runtime.tick(64, 0);
    assert(roamer() == raw(2) && f.runtime.active_bank() == 0);
    f.events.clear();
    dynamicBank = 0;
    f.runtime.tick(64, 0);
    assert(roamer() == raw(1));

    // kill_for_entity nulls the dead id in the inactive bank's pointer copy too.
    f.runtime.kill_for_entity(e1);
    assert(!f.runtime.bank_value(1, "hero").bits && f.state.global("hero").value.bits == e0);

    // Banks off: one shared set of globals again, bank 0 values kept.
    f.runtime.enable_banks({}, {});
    f.events.clear();
    f.start("outer", {}, e1);
    f.expect(0, {raw(9), raw(9)}, e1);
    assert(f.runtime.bank_value(1, "score").raw() == raw(9) && f.runtime.active_bank() == 0);

    // assigned_globals: global write targets (=, op=, ++, element/member, vec_/str_ first arg) in the given files only.
    Fixture g;
    g.runtime.load("main.wdl", [](std::string_view file) -> std::string {
        if (file == "main.wdl") return R"wdl(include "player.wdl"; var world; var story_only; function story() { story_only = 1; })wdl";
        return R"wdl(
            var hp; var ammo[3]; VECTOR dist; VECTOR aim; STRING nm; ENTITY* hero; ENTITY* other; var flags; var read_only;
            function act(n) { var loc; loc = 1; n = 2; hp -= 1; ammo[1] = 3; dist.x = 4; hero = my; flags++;
                other.skill1 = 5; vec_set(aim, dist); str_cpy(nm, "x"); if (vec_length(read_only) == 0) { world = 2; } }
        )wdl";
    });
    const std::string_view files[]{"player.wdl"};
    assert(g.runtime.assigned_globals(files)
           == (std::vector<std::string>{"aim", "ammo", "dist", "flags", "hero", "hp", "nm", "world"}));
    assert(g.runtime.function_file(g.runtime.function("story")) == "main.wdl");
}
} // namespace

int main() {
    arithmetic_and_aliases();
    filename_tokens_preserve_macro_boundaries();
    literal_delimiters_reach_file_text_consumers();
    child_first_wait_returns();
    falling_off_returns_last_condition();
    independent_local_arguments_and_context();
    wait_clamps_to_one_frame();
    entity_freeze_pauses_remaining_timer();
    live_freeze_preserves_null_callback_continuations();
    waitt_strict_raw_boundary();
    continuation_late_order_by_profile();
    cancellation_targets_suspended_my();
    expression_wait_snapshots();
    null_cancellation_guards();
    synchronous_local_storage_is_reused();
    yielded_child_retains_parent_reference();
    yielded_children_retain_context_reference_slots();
    synchronous_context_reference_rebinding();
    yielded_child_retains_entity_field_payload();
    bootstrap_and_entity_enqueue_are_deferred();
    entity_teardown_preserves_ui_and_clears_you();
    registered_pointer_cleanup_preserves_copied_values();
    registered_pointer_cleanup_uses_a6_whole_event_mask();
    removal_clears_exposed_context_without_dropping_leases();
    engine_birth_and_load_use_registered_pointer_cleanup();
    lazy_compound_property_tracks_pointer_rebinding();
    declared_dll_function_pointer_dispatch();
    resolved_null_callbacks_return_zero_after_arguments();
    numeric_control_bindings_preserve_references_and_flags();
    digit_leading_identifiers_and_member_aliases();
    malformed_numeric_literal_is_rejected();
    legacy_resource_comma_constructor();
    parenthesized_array_declaration_initializes_storage();
    statement_dispatch_skips_leading_commas();
    numeric_array_initializer_ignores_unpaired_parenthesis();
    implicit_declaration_initializers_and_callback_bindings();
    text_string_property_reads_whitespace_separated_names();
    material_birth_defaults_reach_script_consumers();
    native_expression_compaction_and_compiler_metadata();
    split_operator_expression_compaction();
    yielded_child_retains_parameter_and_builtin_argument();
    object_parameter_members_and_arrays();
    call_arguments_are_captured_right_to_left();
    panel_digits_numeric_binding_warning_policy();
    obsolete_window_is_parsed_without_finalizing();
    coop_banks_isolate_processes();
    std::cout << "WDL arithmetic and scheduler regression passed\n";
}
