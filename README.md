# CSCI-104 Personal Projects

This repository contains personal C++ projects developed alongside USC's **CSCI-104: Data Structures and Object-Oriented Design** course. The projects reinforce data structure implementation, dynamic memory management, generic programming, graph traversal, multi-file object-oriented design, and, with WakeDeck, event-driven network programming with Qt.

The course and this collection are **in progress**. The repository currently contains the Trojan Help Desk Priority Queue, Trojan Campus Explorer and WakeDeck projects.

## Author

**Matteo Benvenuti**  
University of Southern California  
B.S. Computer Science and Business Administration (CSBA)

## Projects

| Project | Description |
|---|---|
| [Trojan Help Desk Priority Queue](./Trojan%20Help%20Desk%20Priority%20Queue/) | Console-based ticket manager using a custom binary min-heap with FIFO ordering among equal-priority tickets |
| [Trojan Campus Explorer](./Trojan%20Campus%20Explorer/) | Campus graph traversal using interchangeable FIFO and alphabetical priority frontiers |
| [WakeDeck](./WakeDeck/) | macOS Wake-on-LAN tool that wakes a saved computer, waits until its service (Sunshine or any TCP port) is actually ready, and will open the client; C++17 with Qt 6, command line today, graphical interface in progress |

## Tech Stack

- **Language:** C++ (C++17 standard)
- **Paradigm:** Object-Oriented Programming (OOP)
- **Data structures:** Dynamic array-backed binary min-heap, graph adjacency lists, maps, sets, deques, and STL priority queues
- **Frameworks (WakeDeck):** Qt 6 (Core, Network, Test) for the event loop, sockets and tests; CMake driven by a Makefile
- **Compilation:** `c++ -std=c++17` or `make` (WakeDeck needs CMake and Qt 6, see its README)

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

```bash
cd WakeDeck
make
./build/wakedeck-cli --help
```

All three projects support `make run` and `make clean`; WakeDeck also has `make test`, and needs CMake 3.21+ and Qt 6.7+ (`brew install cmake ninja qt`) because it is a Qt application. To compile the Help Desk manually from its folder:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp ticket.cpp priorityqueue.cpp -o helpdesk
./helpdesk
```

To compile Campus Explorer manually from its folder:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp campus.cpp -o campus_explorer
./campus_explorer
```

See the [Help Desk README](./Trojan%20Help%20Desk%20Priority%20Queue/README.md), [Campus Explorer README](./Trojan%20Campus%20Explorer/README.md) and [WakeDeck README](./WakeDeck/README.md) for features, class structure, expected behavior, and project-specific details.

## Course Context

These projects extend the programming foundations from CSCI-103 through hands-on work with data structures and their operations. The Help Desk project focuses on priority queues, heap ordering, stable tie-breaking, dynamic array resizing, deep copies, and resource cleanup. Campus Explorer adds STL containers, abstract interfaces, inheritance, runtime polymorphism, templates, functors, and graph traversal. WakeDeck goes beyond the course material: a multi-library C++17 application with Qt, asynchronous sockets without threads, a state machine, interfaces with injected fakes for testing, JSON serialization with validation, and a test suite at every level from bytes to command line.
