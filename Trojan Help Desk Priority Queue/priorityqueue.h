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
        int getSize() const;
        void print() const;

    private:
        Ticket* data;
        int currentTicketNum;
        int capacity;

        bool higherPriority(const Ticket& first, const Ticket& second) const;
        void trickleUp(int index);
        void trickleDown(int index);
};

#endif