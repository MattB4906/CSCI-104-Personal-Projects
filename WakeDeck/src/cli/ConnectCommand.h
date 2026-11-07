// The `check` and `connect` commands. They need Qt's event loop, so they live
// apart from the synchronous `wake` command in main.cpp.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace wakedeck::cli {

void printCheckUsage(std::ostream& out);
void printConnectUsage(std::ostream& out);

// `args` holds everything after the program name; args[0] is the command.
int runCheck(int argc, char** argv, const std::vector<std::string>& args);
int runConnect(int argc, char** argv, const std::vector<std::string>& args);

}  // namespace wakedeck::cli
