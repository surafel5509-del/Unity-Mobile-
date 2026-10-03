#include "prism_test.h"
#include "prism/script/prismscript.h"
using namespace prism::script;

PRISM_TEST(script_hello_world) {
    Interpreter vm; std::string output;
    vm.set_output_sink([&](const std::string& s){output+=s;});
    PRISM_CHECK(vm.run_source("print(\"Hello, Prism!\");"));
    PRISM_CHECK_STR(output,"Hello, Prism!");
}
PRISM_TEST(script_arithmetic_functions) {
    Interpreter vm;
    PRISM_CHECK(vm.run_source(R"(
        func fib(n) { if (n < 2) { return n; } return fib(n-1)+fib(n-2); }
        var answer = fib(10);
        var v = clamp(42, 0, 10) + lerp(0, 10, 0.5);
    )"));
    Value v;
    PRISM_CHECK(vm.get_global("answer",v)); PRISM_CHECK_NEAR(v.n,55,1e-9);
    PRISM_CHECK(vm.get_global("v",v)); PRISM_CHECK_NEAR(v.n,15,1e-9);
}
PRISM_TEST(script_classes_lifecycle) {
    Interpreter vm;
    PRISM_CHECK(vm.run_source(R"(
        class Player {
            var x = 0;
            var speed = 4;
            func update(dt) { this.x += this.speed * dt; }
        }
        var player = new Player();
        player.update(0.5);
        player.update(0.5);
        var result = player.x;
    )"));
    Value v; PRISM_CHECK(vm.get_global("result",v)); PRISM_CHECK_NEAR(v.n,4,1e-9);
    PRISM_CHECK(vm.has_class("Player"));
}
PRISM_TEST(script_collections_loops) {
    Interpreter vm;
    PRISM_CHECK(vm.run_source(R"(
        var items = [1,2,3,4];
        push(items,5);
        var sum = 0;
        for x in items { sum += x; }
        var doubled = map(items, |x| x*2);
        var m = { score: sum, name: "Ray" };
        var result = m.score + doubled[0];
    )"));
    Value v; PRISM_CHECK(vm.get_global("result",v)); PRISM_CHECK_NEAR(v.n,17,1e-9);
}
PRISM_TEST(script_error_report) {
    Interpreter vm;
    PRISM_CHECK(!vm.run_source("let x = ;","bad.prism"));
    PRISM_CHECK(vm.last_error().find("bad.prism")!=std::string::npos);
    PRISM_CHECK(!vm.run_source("var x = 1 / 0;","zero.prism"));
    PRISM_CHECK(vm.last_error().find("division by zero")!=std::string::npos);
}
PRISM_TEST(script_sample_file) {
    Interpreter vm;
    // relative to the repository root; build_tests.sh cd's there
    std::FILE* f=std::fopen("android/app/src/main/assets/sample.prism","rb");
    PRISM_CHECK(f!=nullptr);
    if (!f) return;
    std::string src; char buf[1024];std::size_t n;
    while((n=std::fread(buf,1,sizeof(buf),f)))src.append(buf,n);
    std::fclose(f);
    PRISM_CHECK(vm.run_source(src,"sample.prism"));
    PRISM_CHECK(vm.has_class("DemoGame"));
}
