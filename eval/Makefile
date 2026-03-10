CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic
CPPFLAGS := -Iinclude $(shell pkg-config --cflags nlohmann_json 2>/dev/null || echo -I/opt/homebrew/include)

TARGET := predicate
SRCS   := src/main.cpp src/test_predicate.cpp src/parser.cpp src/evaluator.cpp src/predicate_dag.cpp
OBJS   := $(SRCS:.cpp=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) --test-predicate ../aig/predicate.json

clean:
	rm -f $(OBJS) $(TARGET)
