#ifndef CAMPUS_H
#define CAMPUS_H

#include <map>
#include <string>
#include <vector>

using Graph = std::map<std::string, std::vector<std::string>>;

struct AlphabeticalFirst {
    bool operator()(const std::string& a, const std::string& b) const;
};

Graph makeCampusGraph();

#endif