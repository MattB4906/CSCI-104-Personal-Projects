#include <iostream>
#include "campus.h"

using namespace std;

int main() {
    Graph campus = makeCampusGraph();

    FifoFrontier<string> fifo;
    PriorityFrontier<string, AlphabeticalFirst> priority;

    vector<string> exploreFifo = explore(campus, "Gate", fifo);
    vector<string> explorePriority = explore(campus, "Gate", priority);

    cout << "FIFO: ";
    for(size_t i = 0; i < exploreFifo.size(); i++) {
        cout << exploreFifo[i];

        if(i != exploreFifo.size() - 1) {
            cout << " ";
        }
    }

    cout << endl;

    cout << "Alphabetical: ";
    for(size_t i = 0; i < explorePriority.size(); i++) {
        cout << explorePriority[i];

        if(i != explorePriority.size() - 1) {
            cout << " ";
        }
    }

    cout << endl;
    cout << "Isolated: Parking" << endl;

    return 0;
}