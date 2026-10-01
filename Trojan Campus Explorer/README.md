# Trojan Campus Explorer

A graph traversal demo written in C++17. The program explores a small campus map through one shared function, using either a FIFO frontier or an alphabetical priority frontier to choose the next location.

## Purpose

This project was built to practise **abstract data types**, **STL containers**, **inheritance and polymorphism**, **templates and functors**, and **graph traversal** alongside CSCI-104 Units 4–10.

The frontier classes wrap STL containers behind a common interface. The priority frontier uses `std::priority_queue`; the separate Trojan Help Desk project implements a binary heap manually.

## Features

- Represent campus connections with a map from location names to neighbor vectors
- Explore reachable locations in breadth-first order with a FIFO frontier
- Choose the alphabetically earliest waiting location with a priority frontier
- Track discovered locations in a set so cycles do not schedule a location twice
- Use the same traversal function with both frontier implementations
- Handle an isolated location through its empty neighbor vector
- Throw `std::out_of_range` when either frontier is popped while empty

## Campus Map

Each connection appears in both directions. Neighbor order is preserved in the vectors and determines FIFO discovery order.

| Location | Neighbors, in order |
|---|---|
| Gate | Library, Cafe |
| Library | Gate, Stadium |
| Cafe | Gate, Dorm |
| Dorm | Cafe, Gym |
| Gym | Dorm, Stadium |
| Stadium | Library, Gym |
| Parking | None |

## Class & File Structure

```text
Trojan Campus Explorer/
├── main.cpp       # Runs and prints the three demonstrations
├── campus.h       # Graph alias, comparator, and function declarations
├── campus.cpp     # Campus construction, comparison, and traversal
├── frontier.h     # Abstract interface and both template implementations
├── Makefile       # C++17 build, run, and clean targets
├── .gitignore     # Executable, object files, and macOS generated files
└── README.md      # Project documentation
```

| Component | Responsibility |
|---|---|
| `Graph` | Alias for `std::map<std::string, std::vector<std::string>>` |
| `Frontier<T>` | Abstract interface with virtual `push`, `pop`, `empty`, and destructor |
| `FifoFrontier<T>` | FIFO behavior backed by `std::deque<T>` |
| `PriorityFrontier<T, Compare>` | Priority behavior backed by `std::priority_queue` |
| `AlphabeticalFirst` | Functor that gives earlier location names higher removal priority |
| `makeCampusGraph()` | Constructs and returns the fixed campus graph |
| `explore()` | Returns reachable locations in frontier removal order |

Template definitions remain in `frontier.h` so they are visible when instantiated. Each concrete frontier owns its STL container; callers pass a frontier to `explore` through a base-class reference.

## How It Works

The traversal marks the starting location as discovered and adds it to the frontier. It repeatedly removes the next waiting location, records it in the output vector, and schedules its undiscovered neighbors in their stored order.

A location is marked when it is scheduled. This prevents cycles from repeatedly adding the same locations. Exploration finishes when the frontier is empty.

The FIFO frontier produces BFS order. The alphabetical frontier prioritizes names among currently waiting locations; it does not calculate weighted shortest paths. The returned vector records exploration order rather than a route between two endpoints.

## How to Compile & Run

Run these commands from the project directory with a C++17 compiler and Make installed:

```bash
make
./campus_explorer
```

Build and run in one command:

```bash
make run
```

To compile manually:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic main.cpp campus.cpp -o campus_explorer
./campus_explorer
```

Expected program output:

```text
FIFO: Gate Library Cafe Stadium Dorm Gym
Alphabetical: Gate Cafe Dorm Gym Library Stadium
Isolated: Parking
```

The isolated demonstration calls `explore` from Parking and reuses the FIFO frontier after its earlier traversal has emptied it.

Remove the executable and object files with:

```bash
make clean
```

## Runtime Complexity

For a graph with V vertices and E edges, treating string operations as constant-cost:

- Both traversal modes have an `O((V + E) log(V + 1))` time upper bound with the ordered maps and sets used here.
- Additional traversal storage is `O(V)` for discovered locations, the frontier, and output.
- The graph representation uses `O(V + E)` storage.

Array-indexed BFS can run in `O(V + E)`; this implementation adds ordered-container lookup costs. An undirected connection is stored twice, once in each endpoint's neighbor vector.

## Current Scope

- The campus map and demonstration starting locations are fixed in the source.
- `explore` expects an initially empty frontier, an existing starting location, and valid graph entries for every neighbor.
- The program uses unweighted connections and performs reachability and traversal-order demonstrations.
- `main.cpp` provides the three demonstrations. There is no separate test executable or test target in this project.
