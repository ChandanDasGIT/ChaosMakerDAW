// Runs the backend tests. No Qt and no sound card.
//     backendtests                 every test
//     backendtests tempomap        tests whose name contains "tempomap"
//     backendtests --list          the names of all tests
#include "testkit.h"

#include <algorithm>
#include <cstdio>
#include <string>

int main(int argc, char* argv[])
{
    const std::string filter = argc > 1 ? argv[1] : "";
    auto& tests = testkit::allTests();
    std::sort(tests.begin(), tests.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    if (filter == "--list") {
        for (const auto& test : tests) std::printf("%s\n", test.name.c_str());
        return 0;
    }

    int ran = 0;
    for (const auto& test : tests) {
        if (test.name.find(filter) == std::string::npos) continue;
        std::printf("%s\n", test.name.c_str());
        test.run();
        ++ran;
    }

    if (ran == 0) {                                        // a typo must not look like a pass
        std::printf("No test matches \"%s\". The first word of TEST must match the file name.\n", filter.c_str());
        return 1;
    }
    std::printf("\n%d tests, %d failed checks.\n", ran, testkit::failures());
    return testkit::failures() == 0 ? 0 : 1;
}
