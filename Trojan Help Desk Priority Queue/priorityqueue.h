#ifndef PRIORITYQUEUE_H
#define PRIORITYQUEUE_H

#include "ticket.h"

class PriorityQueue {
    public:
        PriorityQueue();
        PriorityQueue(const PriorityQueue& other);
        PriorityQueue& operator=(const PriorityQueue& other);
        ~PriorityQueue();
        void add(const Ticket& ticket);
        const Ticket& peek() const;
        Ticket remove();
        bool isEmpty() const;
        std::size_t getSize() const;
        void print() const;
        bool containsId(int id) const;
        bool cancelById(int id);
        bool changePriority(int id, int newPriority);

    private:
        Ticket* data;
        std::size_t currentTicketNum;
        std::size_t capacity;

        bool higherPriority(const Ticket& first, const Ticket& second) const;
        void trickleUp(std::size_t index);
        void trickleDown(std::size_t index);
};

#endif