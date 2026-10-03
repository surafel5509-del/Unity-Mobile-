// PRISM ENGINE — starter-kit content and PrismScript execution checks.
#include "prism_test.h"
#include "prism/script/prismscript.h"

#include <cstdio>

using namespace prism::script;

PRISM_TEST(script_six_offline_starter_kit_gameplay_sources_execute) {
    const char* files[] = {
        "samples/skyrail_2d/scripts/main.prism",
        "samples/ember_dungeon_2d/scripts/main.prism",
        "samples/chromatic_tiles_2d/scripts/main.prism",
        "samples/neon_circuit_3d/scripts/main.prism",
        "samples/orbit_foundry_3d/scripts/main.prism",
        "samples/wildlight_tactics_3d/scripts/main.prism",
    };
    for (const char* path : files) {
        std::FILE* f = std::fopen(path, "rb");
        PRISM_CHECK(f != nullptr);
        if (!f) continue;
        std::string source;
        char buf[1024];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f))) source.append(buf, n);
        std::fclose(f);
        Interpreter vm;
        std::string output;
        vm.set_output_sink([&](const std::string& s) { output += s; });
        PRISM_CHECK(vm.run_source(source, path));
        PRISM_CHECK(output.find("ready") != std::string::npos);
    }
}
