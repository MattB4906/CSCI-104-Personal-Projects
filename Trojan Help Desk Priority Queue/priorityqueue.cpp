#include "priorityqueue.h"
#include <stdexcept>
#include <iostream>

bool PriorityQueue::higherPriority(const Ticket& first, const Ticket& second) const
{
    if(first.getPriority() == second.getPriority()) {
        return first.getArrivalNumber() < second.getArrivalNumber();
    }

    return first.getPriority() < second.getPriority();
}

void PriorityQueue::trickleUp(std::size_t index)
{
    while(index > 0) {
        std::size_t parent = (index - 1) / 2;

        if(!higherPriority(data[index], data[parent])) {
            return;
        }

        Ticket temp = data[index];
        data[index] = data[parent];
        data[parent] = temp;
        
        index = parent;
    }
}

void PriorityQueue::trickleDown(std::size_t index)
{
    while(true) {
        std::size_t lChild = 2 * index + 1;
        std::size_t rChild = 2 * index + 2;
    
        if(lChild >= currentTicketNum) {
            return;
        }
    
        int bestChild = lChild;

        if(rChild < currentTicketNum && higherPriority(data[rChild], data[lChild])) {
            bestChild = rChild;
        }
    
        if(!higherPriority(data[bestChild], data[index])) {
            return;
        }
        
        Ticket temp = data[index];
        data[index] = data[bestChild];
        data[bestChild] = temp;
        index = bestChild;
    }
}

PriorityQueue::PriorityQueue() : currentTicketNum(0), capacity(4)
{
    data = new Ticket[capacity];
}

PriorityQueue::PriorityQueue(const PriorityQueue& other)
: data(new Ticket[other.capacity]), currentTicketNum(other.currentTicketNum), capacity(other.capacity)
{
    for(std::size_t i = 0; i < currentTicketNum; i++) {
        data[i] = other.data[i];
    }
}

PriorityQueue& PriorityQueue::operator=(const PriorityQueue& other)
{
   if(this == &other) {
        return *this;
   }

   Ticket* newData = new Ticket[other.capacity];
   
   for(std::size_t i = 0; i < other.currentTicketNum; i++) {
        newData[i] = other.data[i];
   }
   
   delete[] data;

   data = newData;
   currentTicketNum = other.currentTicketNum;
   capacity = other.capacity;

   return *this;
}

PriorityQueue::~PriorityQueue()
{
    delete[] data;
}

void PriorityQueue::add(const Ticket& ticket)
{
    if(currentTicketNum >= capacity) {
        capacity = capacity * 2;

        Ticket* temp = new Ticket[capacity];
        for(std::size_t i = 0; i < currentTicketNum; i++) {
            temp[i] = data[i];
        }

        delete[] data;

        data = temp;
    }

    data[currentTicketNum] = ticket;
    trickleUp(currentTicketNum);
    currentTicketNum++;
}

const Ticket& PriorityQueue::peek() const
{
    if(currentTicketNum == 0) {
        throw std::invalid_argument("No ticket present");
    }

    return data[0];
}

Ticket PriorityQueue::remove()
{
    if(currentTicketNum == 0) {
        throw std::invalid_argument("No ticket present");
    }

    Ticket temp = data[0];

    data[0] = data[currentTicketNum - 1];
    currentTicketNum--;
    trickleDown(0);

    return temp;
} 

bool PriorityQueue::isEmpty() const
{
    return !currentTicketNum;
}

std::size_t PriorityQueue::getSize() const
{
    return currentTicketNum;
}

void PriorityQueue::print() const
{
    std::cout << "Current tickets:" << std::endl;

    if(isEmpty()) {
        std::cout << "(empty)" << std::endl;

        return;
    }

    for(std::size_t i = 0; i < currentTicketNum; i++) {
        std::cout << data[i] << std::endl;
    }
}

bool PriorityQueue::containsId(int id) const
{
    for(size_t i = 0; i < currentTicketNum; i++) {
        Ticket temp = data[i];

        if(temp.getId() == id) {
            return true;
        }
    }

    return false;
}

bool PriorityQueue::cancelById(int id)
{   
    for(size_t i = 0; i < currentTicketNum; i++) {
        Ticket temp = data[i];
        
        if(temp.getId() == id) {
            data[i] = data[currentTicketNum - 1];
            
            if(i == currentTicketNum - 1) {
                currentTicketNum--;

                return true;
            }
            
            currentTicketNum--;
            
            if(i > 0) {
                std::size_t parent = (i - 1) / 2;
                
                if(higherPriority(data[i], data[parent])) {
                    trickleUp(i);
    
                    return true;
                }
    
                else {
                    trickleDown(i);
    
                    return true;
                }       
            }

            else {
               trickleDown(0);
               
               return true;
            }
        }
    }

    return false;
}

bool PriorityQueue::changePriority(int id, int newPriority)
{
    if(newPriority < 1 || newPriority > 5) {
        std::cout << "Priority must be between 1 and 5" << std::endl;

        return false;
    }

    for(size_t i = 0; i < currentTicketNum; i++) {
        Ticket temp = data[i];

        if(temp.getId() == id) {
            Ticket t(id, newPriority, temp.getDescription(), temp.getArrivalNumber());
            data[i] = t;

            if(i > 0) {
                std::size_t parent = (i - 1) / 2;
                
                if(higherPriority(data[i], data[parent])) {
                    trickleUp(i);
    
                    return true;
                }
    
                else {
                    trickleDown(i);
    
                    return true;
                }
            }
        }
    }

    std::cout << "Id not found" << std::endl;

    return false;
}