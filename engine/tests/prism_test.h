// PRISM ENGINE — tests/prism_test.h : minimal zero-dependency test runner.
// Note: pass constructor arguments with parentheses (Vec3(1,2,3)), never braces,
// because commas inside braces would be parsed as macro argument separators.
#pragma once
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

namespace prism::test {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks()   { static int c = 0; return c; }

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) { registry().push_back({std::move(name), std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& what) {
    ++failures();
    std::printf("    FAIL %s:%d  %s\n", file, line, what.c_str());
}

inline int run_all(const char* filter = nullptr) {
    int ran = 0;
    for (auto& c : registry()) {
        if (filter && c.name.find(filter) == std::string::npos) continue;
        int before = failures();
        std::printf("[ RUN  ] %s\n", c.name.c_str());
        c.fn();
        ++ran;
        std::printf("[ %s ] %s\n", failures() == before ? " OK " : "FAIL", c.name.c_str());
    }
    std::printf("\n==== %d test(s), %d check(s), %d failure(s) ====\n", ran, checks(), failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace prism::test

#define PRISM_TEST(name)                                                        \
    static void name();                                                         \
    static ::prism::test::Registrar reg_##name(#name, name);                    \
    static void name()

#define PRISM_CHECK(...)                                                        \
    do {                                                                        \
        ++::prism::test::checks();                                              \
        if (!(__VA_ARGS__)) ::prism::test::fail(__FILE__, __LINE__, #__VA_ARGS__); \
    } while (0)

#define PRISM_CHECK_EQ(a, b)                                                    \
    do {                                                                        \
        ++::prism::test::checks();                                              \
        auto va = (a); auto vb = (b);                                           \
        if (!(va == vb)) {                                                      \
            std::ostringstream os;                                              \
            os << #a << " == " << #b << "  (" << va << " vs " << vb << ")";     \
            ::prism::test::fail(__FILE__, __LINE__, os.str());                  \
        }                                                                       \
    } while (0)

#define PRISM_CHECK_NEAR(a, b, eps)                                             \
    do {                                                                        \
        ++::prism::test::checks();                                              \
        double va = (a);                                                        \
        double vb = (b);                                                        \
        double ve = (eps);                                                      \
        if (!(std::fabs(va - vb) <= ve)) {                                      \
            std::ostringstream os;                                              \
            os << #a << " ~= " << #b << "  (" << va << " vs " << vb << ")";     \
            ::prism::test::fail(__FILE__, __LINE__, os.str());                  \
        }                                                                       \
    } while (0)

#define PRISM_CHECK_STR(a, b)                                                   \
    do {                                                                        \
        ++::prism::test::checks();                                              \
        std::string va = (a), vb = (b);                                         \
        if (va != vb) {                                                         \
            std::ostringstream os;                                              \
            os << #a << " == " << #b << "  (\"" << va << "\" vs \"" << vb << "\")"; \
            ::prism::test::fail(__FILE__, __LINE__, os.str());                  \
        }                                                                       \
    } while (0)

#define PRISM_CHECK_NE(a, b)                                                    \
    do {                                                                        \
        ++::prism::test::checks();                                              \
        auto va = (a); auto vb = (b);                                           \
        if ((va == vb)) {                                                       \
            std::ostringstream os;                                              \
            os << #a << " != " << #b << "  (both " << va << ")";                \
            ::prism::test::fail(__FILE__, __LINE__, os.str());                  \
        }                                                                       \
    } while (0)
