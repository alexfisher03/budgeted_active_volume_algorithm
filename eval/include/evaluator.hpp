#pragma once

#include "predicate_dag.hpp"

#include <string>
#include <tuple>
#include <unordered_map>

struct EvalResult {
  bool root_value = false;
  std::unordered_map<int, bool> node_values; // node id → value
};

EvalResult evaluate(const PredicateDag &dag,
                    const std::unordered_map<int, bool> &input_assignment);

// Key: (signal, bit, time)  →  bool value
using SemanticKey = std::tuple<std::string, int, int>;

struct SemanticKeyHash {
  std::size_t operator()(const SemanticKey &k) const;
};

using SemanticAssignment =
    std::unordered_map<SemanticKey, bool, SemanticKeyHash>;

// Build a literal-keyed assignment from a human-readable semantic assignment.
// Throws if a semantic key does not match any input in the DAG.
std::unordered_map<int, bool> build_assignment(const PredicateDag &dag,
                                               const SemanticAssignment &sem);
