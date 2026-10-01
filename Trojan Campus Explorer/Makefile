CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic

TARGET = campus_explorer
OBJECTS = main.o campus.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

main.o: main.cpp campus.h frontier.h
	$(CXX) $(CXXFLAGS) -c main.cpp

campus.o: campus.cpp campus.h frontier.h
	$(CXX) $(CXXFLAGS) -c campus.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all run clean
