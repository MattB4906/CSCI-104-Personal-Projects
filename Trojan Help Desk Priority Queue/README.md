# Trojan Help Desk Priority Queue

A console-based help desk ticket manager written in C++. The program stores support tickets in a custom priority queue so urgent requests are processed first while preserving first-in, first-out order among tickets with the same priority.

## Purpose

This project was built to practice **binary heaps**, **dynamic memory management**, **copy control**, and **multi-file object-oriented design**. Instead of using `std::priority_queue`, the project implements an array-backed min-heap from scratch, including automatic capacity growth and explicit trickle-up and trickle-down operations.

## Features

### Ticket Management

- Add a ticket with a numeric ID, priority, and description
- View the next ticket without removing it
- Process and remove the next ticket
- List every ticket currently stored in the queue
- Detect attempts to view or process an empty queue

### Priority Rules

- Priorities range from `1` to `5`
- Lower numbers represent more urgent tickets, so priority `1` is processed first
- Tickets with the same priority are processed in first-in, first-out order
- An internal arrival number provides stable tie-breaking without requiring user input

### Dynamic Heap Storage

- Tickets are stored in a dynamically allocated array representing a binary min-heap
- The queue begins with space for four tickets and doubles its capacity whenever it becomes full
- Adding a ticket restores heap order with a trickle-up operation
- Removing a ticket restores heap order with a trickle-down operation
- `peek()` runs in constant time, while insertion and removal run in logarithmic time

### Interactive Commands

| Command | Action |
|---|---|
| `A` | Add a new help desk ticket |
| `N` | Display the next ticket to be processed |
| `P` | Process and remove the next ticket |
| `L` | List the tickets stored in the heap |
| `Q` | Quit the program |

The list command displays the queue's internal heap order. The first ticket is always the next one to process, but the remaining entries are not guaranteed to appear in fully sorted order.

## Class & File Structure

```
Trojan Help Desk Priority Queue/
├── main.cpp              # Interactive command menu and input validation
├── ticket.h/.cpp        # Ticket data model, comparison, and formatted output
├── priorityqueue.h/.cpp # Dynamic array-backed binary min-heap
└── Makefile              # C++17 build, run, and clean targets
```

### Object Relationships

```
PriorityQueue
└── Ticket[]  (dynamic array representing a binary min-heap)
    ├── id
    ├── priority
    ├── description
    └── arrival number
```

`PriorityQueue` owns its dynamically allocated `Ticket` array. Its copy constructor and assignment operator create independent copies of that storage, and its destructor releases the array.

## How It Works

1. The user enters a ticket ID, a priority from `1` to `5`, and a description.
2. The program assigns the ticket an increasing arrival number and inserts it at the end of the heap.
3. The new ticket trickles upward until the priority and arrival-order rules are restored.
4. When a ticket is processed, the root ticket is removed and the last ticket moves to the root.
5. The replacement trickles downward until the heap property is restored.

## How to Compile & Run

```bash
make
./helpdesk
```

The project can also be built and launched in one command:

```bash
make run
```

To compile manually:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp ticket.cpp priorityqueue.cpp -o helpdesk
./helpdesk
```

## Concepts Demonstrated

- Binary min-heap implementation using a contiguous array
- Priority-queue operations: insertion, peek, and removal
- Stable FIFO tie-breaking for equal-priority elements
- Dynamic array resizing with a doubling strategy
- Deep copying through a copy constructor and copy-assignment operator
- Resource cleanup with destructors
- Operator overloading (`==` and `<<`) for `Ticket` objects
- Const-correct member functions and reference returns
- Exception-based empty-queue protection
- Input validation and interactive menu design
- Multi-file compilation and Makefile automation
