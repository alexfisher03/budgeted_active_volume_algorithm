#include "predicate_dag.hpp"

void PredicateDag::build_lookups() {
  node_by_id.clear();
  input_by_lit.clear();
  input_lit_set.clear();

  for (const auto &node : nodes) {
    node_by_id[node.id] = &node;
  }

  for (const auto &iv : inputs) {
    input_by_lit[iv.lit] = &iv;
  }

  for (int lit : input_lits) {
    input_lit_set.insert(lit);
  }
}
