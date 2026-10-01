#ifndef CAMPUS_H
#define CAMPUS_H

#include <map>
#include <string>
#include <vector>
#include "frontier.h"

using Graph = std::map<std::string, std::vector<std::string>>;

struct AlphabeticalFirst {
    bool operator()(const std::string& a, const std::string& b) const;
};

Graph makeCampusGraph();
std::vector<std::string> explore(const Graph& graph, const std::string& start, Frontier<std::string>& frontier);

#endif