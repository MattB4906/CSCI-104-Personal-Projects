# CSCI-104 Personal Projects

This repository contains personal C++ projects developed alongside USC's **CSCI-104: Data Structures and Object-Oriented Design** course. The projects reinforce data structure implementation, dynamic memory management, and multi-file object-oriented design.

The course and this collection are **in progress**. The repository currently contains the Trojan Help Desk Priority Queue project.

## Author

**Matteo Benvenuti**  
University of Southern California  
B.S. Computer Science and Business Administration (CSBA)

## Projects

| Project | Description |
|---|---|
| [Trojan Help Desk Priority Queue](./Trojan%20Help%20Desk%20Priority%20Queue/) | Console-based ticket manager using a custom binary min-heap with FIFO ordering among equal-priority tickets |

## Tech Stack

- **Language:** C++ (C++17 standard)
- **Paradigm:** Object-Oriented Programming (OOP)
- **Data structures:** Dynamic array-backed binary min-heap
- **Compilation:** `c++ -std=c++17` or `make`

## How to Compile & Run

Each project lives in its own subdirectory. From the repository root:

```bash
cd "Trojan Help Desk Priority Queue"
make
./helpdesk
```

The project also supports `make run` and `make clean`. To compile manually from its folder:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp ticket.cpp priorityqueue.cpp -o helpdesk
./helpdesk
```

See the [project README](./Trojan%20Help%20Desk%20Priority%20Queue/README.md) for features, commands, class structure, and heap behavior.

## Course Context

These projects extend the programming foundations from CSCI-103 through hands-on work with data structures and their operations. The current project focuses on priority queues, heap ordering, stable tie-breaking, dynamic array resizing, deep copies, and resource cleanup.
