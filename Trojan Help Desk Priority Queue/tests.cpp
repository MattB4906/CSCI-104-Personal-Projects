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
    q.add(t5);
    assert(q.getSize() == 5);

    Ticket t6(106, 4, "Test", 5);
    q.add(t6);
    assert(q.getSize() == 6);

    assert(q.peek() == t5);
    assert(q.remove() == t5);
    assert(q.getSize() == 5);

    assert(q.peek() == t2);
    assert(q.remove() == t2);
    assert(q.getSize() == 4);

    assert(q.peek() == t4);
    assert(q.remove() == t4);
    assert(q.getSize() == 3);

    assert(q.peek() == t1);
    assert(q.remove() == t1);
    assert(q.getSize() == 2);

    assert(q.peek() == t6);
    assert(q.remove() == t6);
    assert(q.getSize() == 1);

    assert(q.peek() == t3);
    assert(q.remove() == t3);
    assert(q.getSize() == 0);
    assert(q.isEmpty());

    PriorityQueue q1;
    Ticket t_1(101, 1, "Test", 0);
    Ticket t_2(102, 2, "Test", 1);
    Ticket t_3(103, 3, "Test", 2);

    q1.add(t_1);
    q1.add(t_2);
    q1.add(t_3);

    PriorityQueue q2 = q1;

    assert(q1.getSize() == 3 && q2.getSize() == 3);
    assert(q1.peek() == t_1 && q2.peek() == t_1);

    assert(q2.remove() == t_1);
    assert(q2.peek() == t_2 && q2.getSize() == 2);
    assert(q1.peek() == t_1 && q1.getSize() == 3);
    
    Ticket t_4(104, 0, "Test", 3);
    q1.add(t_4);
    
    assert(q1.peek() == t_4 && q1.getSize() == 4);
    assert(q2.peek() == t_2 && q2.getSize() == 2);

    assert(q1.remove() == t_4);
    assert(q1.getSize() == 3);

    assert(q1.peek() == t_1);
    assert(q.remove() == t_1);
    assert(q.getSize() == 2);

    assert(q1.peek() == t_2);
    assert(q1.remove() == t_2);
    assert(q1.getSize() == 1);

    assert(q1.peek() == t_3);
    assert(q1.remove() == t_3);
    assert(q1.getSize() == 0);
    assert(q1.isEmpty());

    assert(q2.peek() == t_2);
    assert(q2.remove() == t_2);
    assert(q.getSize() == 1);

    assert(q2.peek() == t_3);
    assert(q2.remove() == t_3);
    assert(q2.getSize() == 0);
    assert(q2.isEmpty());    

    return 0;
}