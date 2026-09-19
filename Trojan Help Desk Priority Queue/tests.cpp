#include "priorityqueue.h"
#include "ticket.h"
#include <cassert>

using namespace std;

int main() {
    PriorityQueue q;

    assert(q.isEmpty());
    assert(q.getSize() == 0);

    Ticket t(123, 1, "Test", 0);
    q.add(t);

    assert(!q.isEmpty());
    assert(q.getSize() == 1);
    assert(q.peek() == t);
    assert(q.getSize() == 1);
    assert(q.remove() == t);

    assert(q.isEmpty());
    assert(q.getSize() == 0);

    Ticket t1(101, 4, "Test", 0);
    q.add(t1);
    assert(q.getSize() == 1);

    Ticket t2(102, 2, "Test", 1);
    q.add(t2);
    assert(q.getSize() == 2);

    Ticket t3(103, 5, "Test", 2);
    q.add(t3);
    assert(q.getSize() == 3);

    Ticket t4(104, 2, "Test", 3);
    q.add(t4);
    assert(q.getSize() == 4);

    Ticket t5(105, 1, "Test", 4);
    q.add(t4);
    assert(q.getSize() == 5);

    Ticket t6(106, 4, "Test", 5);
    q.add(t6);
    assert(q.getSize() == 6);

    assert(q.remove() == t5);
    assert(q.getSize() == 5);

    assert(q.remove() == t2);
    assert(q.getSize() == 4);

    assert(q.remove() == t4);
    assert(q.getSize() == 3);

    assert(q.remove() == t1);
    assert(q.getSize() == 2);

    assert(q.remove() == t6);
    assert(q.getSize() == 1);

    assert(q.remove() == t3);
    assert(q.getSize() == 0);
    assert(q.isEmpty());
    
    return 0;
}