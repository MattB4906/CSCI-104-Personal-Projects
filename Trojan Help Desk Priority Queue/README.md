# Trojan Help Desk Priority Queue

A console-based help desk ticket manager written in C++. The program stores support tickets in a custom priority queue so urgent requests are processed first while preserving first-in, first-out order among tickets with the same priority.

## Purpose

This project was built to practice **binary heaps**, **dynamic memory management**, **copy control**, and **multi-file object-oriented design**. Instead of using `std::priority_queue`, the project implements an array-backed min-heap from scratch, including automatic capacity growth and explicit trickle-up and trickle-down operations.

## Features

### Ticket Management

- Add a ticket with a numeric ID, priority, and description
- Reject duplicate IDs among active tickets; IDs can be reused after processing or cancellation
- Cancel any active ticket by ID
- Update a ticket's priority by ID without changing its description or original arrival number
- View the next ticket without removing it
- Process and remove the next ticket
- List every ticket currently stored in the queue
- Detect attempts to view or process an empty queue

### Priority Rules

- Priorities range from `1` to `5`
- Lower numbers represent more urgent tickets, so priority `1` is processed first
- Tickets with the same priority are processed in first-in, first-out order
- An internal arrival number provides stable tie-breaking without requiring user input
- Changing priority preserves the original arrival number, so ties use original arrival order

### Dynamic Heap Storage

- Tickets are stored in a dynamically allocated array representing a binary min-heap
- The queue begins with space for four tickets and doubles its capacity whenever it becomes full
- Adding a ticket restores heap order with a trickle-up operation
- Processing the next ticket restores heap order with a trickle-down operation
- Cancellation and priority updates repair heap order upward or downward from the affected position

### Interactive Commands

| Command | Action |
|---|---|
| `A` | Add a new help desk ticket |
| `N` | Display the next ticket to be processed |
| `P` | Process and remove the next ticket |
| `L` | List the tickets stored in the heap |
| `C` | Cancel an active ticket by ID |
| `U` | Update an active ticket's priority |
| `Q` | Quit the program |

The list command displays the queue's internal heap order. The first ticket is always the next one to process, but the remaining entries are not guaranteed to appear in fully sorted order.

## Class & File Structure

```
Trojan Help Desk Priority Queue/
├── main.cpp            # Interactive command menu and input validation
├── ticket.h/.cpp       # Ticket data model, equality, and formatted output
├── priorityqueue.h/.cpp # Dynamic array-backed binary min-heap
├── tests.cpp           # Assertions for ordering, resizing, and copy assignment
├── Makefile            # C++17 build, run, and clean targets
└── README.md           # Project documentation
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

1. The user enters a ticket ID. The menu rejects IDs already in use before requesting a priority from `1` to `5` and a description.
2. The program assigns the ticket an increasing arrival number and inserts it at the end of the heap.
3. The new ticket trickles upward until the priority and arrival-order rules are restored.
4. When a ticket is processed, the root ticket is removed and the last ticket moves to the root.
5. The replacement trickles downward until the heap property is restored.
6. Cancellation searches for an ID, fills its position with the last ticket, reduces the size, and repairs heap order if a replacement remains.
7. A priority update searches for an ID, replaces only the priority, and repairs heap order without changing the queue size or arrival number.

## Runtime Complexity

For `n` active tickets, treating ticket copies and comparisons as constant-time operations:

| Operation | Time |
|---|---|
| Peek, size, empty check | O(1) |
| Heap insertion without resizing | O(log n) |
| Heap insertion with resizing | O(n) worst case; O(log n) amortized across insertions |
| Process the next ticket | O(log n) |
| Find an ID | O(n) |
| Cancel or update by ID | O(n) search plus O(log n) heap repair, O(n) overall |
| List tickets | O(n), plus description output |

The interactive add command also performs an O(n) duplicate-ID scan. Actual ticket-copy costs depend on description length. Heap storage uses O(n) ticket slots, plus their description storage.

## How to Compile & Run

Run these commands from the project directory with a C++17 compiler and Make installed.

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

To remove the application executable and object files:

```bash
make clean
```

## Verification

The included `tests.cpp` checks empty and single-ticket states, priority ordering, FIFO ties, resizing, and independent copy assignment. Build it separately from `main.cpp`, since both files define an entry point:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic tests.cpp ticket.cpp priorityqueue.cpp -o helpdesk_tests
./helpdesk_tests
rm -f helpdesk_tests
```

Assertions must remain enabled; do not compile this test program with `-DNDEBUG`. The current Makefile does not build or clean the test executable.

A separate temporary verification run also passed 10,000 mixed operations against an independent reference, growth to 1,000 tickets, copy construction and assignment, self-assignment, empty-queue exceptions, and menu workflows. AddressSanitizer and UndefinedBehaviorSanitizer reported no errors during those queue checks. These additional verification scripts are not included in the repository.

## Current Scope

- Tickets are stored in memory for the current session and are not saved when the program exits.
- Commands use the uppercase letters shown in the menu.
- Numeric input checks reject failed conversions and out-of-range priorities. They do not reject every partially numeric entry: for example, `2abc` can be read as `2`.
- Duplicate-ID prevention is enforced by the interactive menu; direct callers of `PriorityQueue::add()` must supply unique active IDs.

## Concepts Demonstrated

- Binary min-heap implementation using a contiguous array
- Priority-queue operations: insertion, peek, removal, cancellation, and priority updates
- Stable FIFO tie-breaking for equal-priority elements
- Dynamic array resizing with a doubling strategy
- Deep copying through a copy constructor and copy-assignment operator
- Resource cleanup with destructors
- Operator overloading (`==` and `<<`) for `Ticket` objects
- Const-correct member functions and reference returns
- Exception-based empty-queue protection
- Input validation and interactive menu design
- Multi-file compilation and Makefile automation
