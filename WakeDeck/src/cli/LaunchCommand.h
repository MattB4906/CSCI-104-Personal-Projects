// The `launch` command: the client step of Wake & Connect on its own, with a
// dry run that shows what would be started. Useful to confirm that Moonlight is
// found and that a custom client's arguments come out right.
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace wakedeck::cli {

void printLaunchUsage(std::ostream& out);

// `args` holds everything after the program name; args[0] is "launch".
int runLaunch(int argc, char** argv, const std::vector<std::string>& args);

}  // namespace wakedeck::cli
