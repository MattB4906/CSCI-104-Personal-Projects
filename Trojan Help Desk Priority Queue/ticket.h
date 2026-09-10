#ifndef TICKET_H
#define TICKET_H

#include <string>
#include <ostream>

class Ticket {
    public:
        Ticket();
        Ticket(int id, int priority, const std::string& description, int arrivalNumber);
        Ticket(const Ticket& other);
        Ticket& operator=(const Ticket& other);
        ~Ticket();
        int getId() const;
        int getPriority() const;
        const std::string& getDescription() const;
        int getArrivalNumber() const;
        bool operator==(const Ticket& rhs) const;
        friend std::ostream& operator<<(std::ostream& os, const Ticket& t);

    private:
        int id;
        int priority;
        std::string description;
        int arrivalNumber;
};

#endif