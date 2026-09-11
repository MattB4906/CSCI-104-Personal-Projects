CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic

TARGET = helpdesk
OBJECTS = main.o ticket.o priorityqueue.o

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

main.o: main.cpp priorityqueue.h ticket.h
	$(CXX) $(CXXFLAGS) -c main.cpp

ticket.o: ticket.cpp ticket.h
	$(CXX) $(CXXFLAGS) -c ticket.cpp

priorityqueue.o: priorityqueue.cpp priorityqueue.h ticket.h
	$(CXX) $(CXXFLAGS) -c priorityqueue.cpp

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)