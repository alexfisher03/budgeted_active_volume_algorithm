#include "evaluator.hpp"

#include <algorithm>
#include <stdexcept>

std::size_t SemanticKeyHash::operator()(const SemanticKey &k) const {
  std::size_t h1 = std::hash<std::string>{}(std::get<0>(k));
  std::size_t h2 = std::hash<int>{}(std::get<1>(k));
  std::size_t h3 = std::hash<int>{}(std::get<2>(k));
  return h1 ^ (h2 * 2654435761u) ^ (h3 * 40503u);
}

static bool eval_literal(int lit,
                         const std::unordered_map<int, bool> &input_assignment,
                         const std::unordered_map<int, bool> &node_values) {
  if (is_constant_false(lit))
    return false;
  if (is_constant_true(lit))
    return true;

  int base = positive_base_literal(lit);
  bool negated = is_negated_literal(lit);

  // try node output first
  {
    int nid = node_id_from_positive_literal(base);
    auto it = node_values.find(nid);
    if (it != node_values.end()) {
      return negated ? !it->second : it->second;
    }
  }

  // try input literal
  {
    auto it = input_assignment.find(base);
    if (it != input_assignment.end()) {
      return negated ? !it->second : it->second;
    }
  }

  throw std::runtime_error("eval_literal: unresolvable literal " +
                           std::to_string(lit));
}

EvalResult evaluate(const PredicateDag &dag,
                    const std::unordered_map<int, bool> &input_assignment) {
  EvalResult result;

  // Sort nodes by ascending id for deterministic evaluation order.
  std::vector<const AndNode *> sorted;
  sorted.reserve(dag.nodes.size());
  for (const auto &n : dag.nodes) {
    sorted.push_back(&n);
  }
  std::sort(sorted.begin(), sorted.end(),
            [](const AndNode *a, const AndNode *b) { return a->id < b->id; });

  // Evaluate each node.
  for (const auto *n : sorted) {
    bool lhs = eval_literal(n->lhs_lit, input_assignment, result.node_values);
    bool rhs = eval_literal(n->rhs_lit, input_assignment, result.node_values);
    result.node_values[n->id] = lhs && rhs;
  }

  // Resolve root.
  result.root_value =
      eval_literal(dag.root_lit, input_assignment, result.node_values);
  return result;
}

std::unordered_map<int, bool> build_assignment(const PredicateDag &dag,
                                               const SemanticAssignment &sem) {

  std::unordered_map<int, bool> assignment;

  for (const auto &[key, value] : sem) {
    const auto &[signal, bit, time] = key;

    bool found = false;
    for (const auto &iv : dag.inputs) {
      if (iv.signal == signal && iv.bit == bit && iv.time == time) {
        assignment[iv.lit] = value;
        found = true;
        break;
      }
    }
    if (!found) {
      throw std::runtime_error(
          "build_assignment: no input for signal=" + signal +
          " bit=" + std::to_string(bit) + " time=" + std::to_string(time));
    }
  }

  return assignment;
}
