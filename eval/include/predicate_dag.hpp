#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// AIGER literal helpers
inline bool is_constant_false(int lit) { return lit == 0; }
inline bool is_constant_true(int lit) { return lit == 1; }
inline bool is_negated_literal(int lit) { return (lit & 1) != 0; }
inline int positive_base_literal(int lit) { return lit & ~1; }
inline int node_id_from_positive_literal(int lit) { return lit >> 1; }

// inputs (2:20)
struct InputVar {
  int lit = 0;
  std::string signal;
  int bit = 0;
  int time = 0;
};

// node V from (V, E)
struct AndNode {
  int id = 0;
  int lhs_lit = 0;
  int rhs_lit = 0;
};

struct PredicateDag {
  std::string export_id;
  int root_lit = 0;
  int constant_false_lit = 0;
  int constant_true_lit = 1;

  std::vector<int> input_lits;
  std::vector<InputVar> inputs;
  std::vector<AndNode> nodes;

  // Helper lookups — populated by build_lookups()
  std::unordered_map<int, const AndNode *> node_by_id;
  std::unordered_map<int, const InputVar *> input_by_lit;
  std::unordered_set<int> input_lit_set;

  void build_lookups();
};
