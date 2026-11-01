#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// A very small test library, written here so the project needs no other.
//
//     TEST(tempomap, six_eight_has_three_beats) {
//         CHECK(something);
//         CHECK_NEAR(value, 3.0, 1e-9);
//     }
//
// The first word of TEST must match the file name: tempomap_test.cpp uses TEST(tempomap, ...).
namespace testkit {

struct Test {
    std::string name;
    void (*run)();
};

inline std::vector<Test>& allTests()
{
    static std::vector<Test> tests;
    return tests;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

inline bool add(const char* suite, const char* name, void (*run)())
{
    allTests().push_back({std::string(suite) + "." + name, run});
    return true;
}

inline void fail(const char* file, int line, const char* message)
{
    std::printf("    FAILED %s:%d: %s\n", file, line, message);
    ++failures();
}

} // namespace testkit

#define TEST(suite, name)                                                                     \
    static void suite##_##name();                                                             \
    [[maybe_unused]] static const bool suite##_##name##_added =                               \
        testkit::add(#suite, #name, suite##_##name);                                          \
    static void suite##_##name()

#define CHECK(condition)                                                                      \
    do {                                                                                      \
        if (!(condition)) testkit::fail(__FILE__, __LINE__, #condition);                      \
    } while (false)

#define CHECK_NEAR(value, expected, tolerance)                                                \
    do {                                                                                      \
        const double testValue = (value);                                                     \
        const double testExpected = (expected);                                               \
        if (!(std::abs(testValue - testExpected) <= (tolerance))) {                           \
            std::printf("    got %.9g, expected %.9g\n", testValue, testExpected);            \
            testkit::fail(__FILE__, __LINE__, #value " near " #expected);                     \
        }                                                                                     \
    } while (false)
