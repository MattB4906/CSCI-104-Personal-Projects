# CSCI-104 Personal Projects

This repository contains personal C++ projects developed alongside USC's **CSCI-104: Data Structures and Object-Oriented Design** course. The projects reinforce data structure implementation, dynamic memory management, generic programming, graph traversal, and multi-file object-oriented design.

The course and this collection are **in progress**. The repository currently contains the Trojan Help Desk Priority Queue and Trojan Campus Explorer projects.

## Author

**Matteo Benvenuti**  
University of Southern California  
B.S. Computer Science and Business Administration (CSBA)

## Projects

| Project | Description |
|---|---|
| [Trojan Help Desk Priority Queue](./Trojan%20Help%20Desk%20Priority%20Queue/) | Console-based ticket manager using a custom binary min-heap with FIFO ordering among equal-priority tickets |
| [Trojan Campus Explorer](./Trojan%20Campus%20Explorer/) | Campus graph traversal using interchangeable FIFO and alphabetical priority frontiers |

## Tech Stack

- **Language:** C++ (C++17 standard)
- **Paradigm:** Object-Oriented Programming (OOP)
- **Data structures:** Dynamic array-backed binary min-heap, graph adjacency lists, maps, sets, deques, and STL priority queues
- **Compilation:** `c++ -std=c++17` or `make`

## How to Compile & Run

Each project lives in its own subdirectory. Run each example separately, starting from the repository root:

```bash
cd "Trojan Help Desk Priority Queue"
make
./helpdesk
```

```bash
cd "Trojan Campus Explorer"
make
./campus_explorer
```

Both projects support `make run` and `make clean`. To compile the Help Desk manually from its folder:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp ticket.cpp priorityqueue.cpp -o helpdesk
./helpdesk
```

To compile Campus Explorer manually from its folder:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp campus.cpp -o campus_explorer
./campus_explorer
```

See the [Help Desk README](./Trojan%20Help%20Desk%20Priority%20Queue/README.md) and [Campus Explorer README](./Trojan%20Campus%20Explorer/README.md) for features, class structure, expected behavior, and project-specific details.

## Course Context

These projects extend the programming foundations from CSCI-103 through hands-on work with data structures and their operations. The Help Desk project focuses on priority queues, heap ordering, stable tie-breaking, dynamic array resizing, deep copies, and resource cleanup. Campus Explorer adds STL containers, abstract interfaces, inheritance, runtime polymorphism, templates, functors, and graph traversal.
