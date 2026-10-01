#include <set>
#include "campus.h"

bool AlphabeticalFirst::operator()(const std::string& a, const std::string& b) const
{
    if(b < a) {
        return true;
    }

    return false;
}

Graph makeCampusGraph()
{
    Graph campus;

    campus["Gate"] = {"Library", "Cafe"};
    campus["Library"] = {"Gate", "Stadium"};
    campus["Cafe"] = {"Gate", "Dorm"};
    campus["Dorm"] = {"Cafe", "Gym"};
    campus["Gym"] = {"Dorm", "Stadium"};
    campus["Stadium"] = {"Library", "Gym"};
    campus["Parking"] = {};

    return campus;
}

std::vector<std::string> explore(const Graph& graph, const std::string& start, Frontier<std::string>& frontier)
{
    std::set<std::string> discovered;
    std::vector<std::string> order;

    discovered.insert(start);
    frontier.push(start);

    while(!frontier.empty()) {
        std::string current = frontier.pop();

        order.push_back(current);

        for(size_t i = 0; i < graph.at(current).size(); i++) {
            std::string neighbor = graph.at(current).at(i);

            if(discovered.find(neighbor) != discovered.end()) {
                continue;
            }

            discovered.insert(neighbor);
            frontier.push(neighbor);
        }
    }

    return order;
}