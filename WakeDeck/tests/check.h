// Minimal test helpers: a CHECK macro, pass/fail counters and a summary line.
// No framework, so the tests build with the same one-line compiler command as
// the rest of the project.
#pragma once

#include <iostream>

namespace check {

inline int total = 0;
inline int failed = 0;

inline void record(bool ok, const char* expression, const char* file, int line) {
    ++total;
    if (!ok) {
        ++failed;
        std::cerr << file << ":" << line << ": CHECK failed: " << expression << "\n";
    }
}

// Prints the summary and returns the process exit code.
inline int finish(const char* suite) {
    std::cout << suite << ": " << (total - failed) << "/" << total << " checks passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace check

// Wrap expressions that contain a top-level comma in an extra pair of parentheses.
#define CHECK(expression) ::check::record(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
