#include "priorityqueue.h"
#include "ticket.h"
#include <iostream>
#include <limits>

using namespace std;

int main() {
    PriorityQueue p{};
    
    char input = 'A';
    int arrivalCounter = 0;
    
    
    
    while(input != 'Q') {
        cout << "Press:" << endl;
        cout << "A: add a ticket" << endl;
        cout << "N: show the next ticket" << endl;
        cout << "P: process/remove the next ticket" << endl;
        cout << "L: list all tickets" << endl;
        cout << "C: cancel a ticket by Id" << endl;
        cout << "U: update a ticket's priority" << endl;
        cout << "Q: quit" << endl;

        if(!(cin >> input)) {
            break;
        }

        if(input == 'A') {
            int id;
            
            cout << "Id: ";
            cin >> id;
            
            if(cin.fail()) {
                cout << "Incorrect input!" << endl;
        
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            if(p.containsId(id)) {
                cout << "Id already exists" << endl;
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            cout << endl;

            int priority;
            cout << "Priority (between 1 and 5): ";
            cin >> priority;

            if(cin.fail()) {
                cout << "Incorrect input!" << endl;
                
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                
                continue;
            }
            
            if(priority < 1 || priority > 5) {
                cout << "Priority must be between 1 and 5!" << endl;
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            cout << endl;

            string description;
            cout << "Description: ";
            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            getline(cin, description);

            if(cin.fail()) {
                cout << "Incorrect input!" << endl;
        
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            cout << endl;

            Ticket t(id, priority, description, arrivalCounter);
            p.add(t);
            arrivalCounter++;

        }

        if(input == 'N') {
            if(p.isEmpty()) {
                cout << "Queue is empty" << endl;

                continue;
            }

            cout << "Next: " << p.peek() << endl;
        }

        if(input == 'P') {
            if(p.isEmpty()) {
                cout << "Queue is empty" << endl;

                continue;
            }

            cout << "Processing: " << p.remove() << endl;
        }

        if(input == 'L') {
            p.print();
        }

        if(input == 'C') {
            int id;
            
            cout << "Id: ";
            cin >> id;
            
            if(cin.fail()) {
                cout << "Incorrect input!" << endl;
        
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            cout << endl;

            if(p.cancelById(id)) {
                cout << "Ticket cancelled" << endl;
            }

            else {
                cout << "Id does not exist" << endl;
            }
        }

        if(input == 'U') {
            int id, priority;
            
            cout << "Id: ";
            cin >> id;

            if(cin.fail()) {
                cout << "Incorrect input!" << endl;
        
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            if(!p.containsId(id)) {
                cout << "Id does not exists" << endl;
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }
            
            cout << endl;

            cout << "Priority: ";
            cin >> priority;

            if(cin.fail()) {
                cout << "Incorrect input!" << endl;
        
                cin.clear();
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            }

            if(priority < 1 || priority > 5) {
                cout << "Priority must be between 1 and 5" << endl;
                cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                continue;
            } 

            cout << endl; 

            if(!p.changePriority(id, priority)) {
                continue;
            }

            else {
                cout << "Priority has been changed" << endl;
            }
        }
    }

    return 0;
}