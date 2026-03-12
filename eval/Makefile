CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic
CPPFLAGS := -Iinclude $(shell pkg-config --cflags nlohmann_json 2>/dev/null || echo -I/opt/homebrew/include)

TARGET := predicate
SRCS   := src/main.cpp src/test_predicate.cpp src/parser.cpp src/evaluator.cpp src/predicate_dag.cpp src/store_all_scheduler.cpp src/budgeted_scheduler.cpp src/oldest_live_policy.cpp src/smallest_fanout_policy.cpp src/depth_aware_policy.cpp src/recompute_aware_policy.cpp src/portfolio_scheduler.cpp src/diagnostic_policy.cpp src/diagnose_portfolio.cpp src/exact_optimal_scheduler.cpp
OBJS   := $(SRCS:.cpp=.o)

.PHONY: all run run-store-all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) --test-predicate ../aig/predicate2.json

run-store-all: $(TARGET)
	./$(TARGET) --run-store-all ../aig/predicate2.json

clean:
	rm -f $(OBJS) $(TARGET)
