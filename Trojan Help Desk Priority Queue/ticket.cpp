#include "ticket.h"

Ticket::Ticket() 
: id(0), priority(0), description(""), arrivalNumber(0) {}

Ticket::Ticket(int id, int priority, const std::string& description, int arrivalNumber)
: id(id), priority(priority), description(description), arrivalNumber(arrivalNumber) {}

Ticket::Ticket(const Ticket& other)
: id(other.id), priority(other.priority), description(other.description), arrivalNumber(other.arrivalNumber) {}

Ticket& Ticket::operator=(const Ticket& other)
{
    if(this == &other) {
        return *this;
    }

    this->id = other.id;
    this->priority = other.priority;
    this->description = other.description;
    this->arrivalNumber = other.arrivalNumber;

    return *this;
}

Ticket::~Ticket() {}

int Ticket::getId() const
{
    return id;
}

int Ticket::getPriority() const
{
    return priority;
}

const std::string& Ticket::getDescription() const
{
    return description;
}

int Ticket::getArrivalNumber() const
{
    return arrivalNumber;
}

bool Ticket::operator==(const Ticket& rhs) const
{
    return this->id == rhs.id && this->priority == rhs.priority && this->description == rhs.description && this->arrivalNumber == rhs.arrivalNumber;
}

std::ostream& operator<<(std::ostream& os, const Ticket& t)
{
    os << "Ticket " << t.id << " | Priority: " << t.priority << " | Description: " << t.description;

    return os;
}