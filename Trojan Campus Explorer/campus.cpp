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