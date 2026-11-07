// The `profile` command: list, show, add, remove, import and export saved
// computers. Works on the profiles file only; it never touches the network.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace wakedeck::cli {

void printProfileUsage(std::ostream& out);

// `args` holds everything after the program name; args[0] is "profile".
int runProfile(const std::vector<std::string>& args);

}  // namespace wakedeck::cli
