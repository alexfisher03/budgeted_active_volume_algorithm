#pragma once

#include "predicate_dag.hpp"
#include "scheduler_types.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

// shared DAG / legality helpers
namespace sched {

inline bool is_input_or_constant_literal(int lit, const PredicateDag &dag) {
  if (is_constant_false(lit) || is_constant_true(lit))
    return true;
  int base = positive_base_literal(lit);
  return dag.input_lit_set.count(base) > 0;
}

// return the internal node id that produces the given literal,
// or -1 if the literal is a constant or primary input
inline int producing_node_id_for_literal(int lit, const PredicateDag &dag) {
  if (is_input_or_constant_literal(lit, dag))
    return -1;
  int base = positive_base_literal(lit);
  int nid = node_id_from_positive_literal(base);
  if (!dag.node_by_id.count(nid))
    throw std::runtime_error("producing_node_id_for_literal: literal " +
                             std::to_string(lit) +
                             " maps to unknown node " + std::to_string(nid));
  return nid;
}

inline bool is_literal_available(int lit, const ScheduleState &state,
                                 const PredicateDag &dag) {
  if (is_input_or_constant_literal(lit, dag))
    return true;
  int nid = producing_node_id_for_literal(lit, dag);
  return state.live_nodes.count(nid) > 0;
}

inline bool can_compute(int node_id, const ScheduleState &state,
                        const PredicateDag &dag) {
  auto it = dag.node_by_id.find(node_id);
  if (it == dag.node_by_id.end())
    return false;
  const AndNode *node = it->second;
  return is_literal_available(node->lhs_lit, state, dag) &&
         is_literal_available(node->rhs_lit, state, dag);
}

inline bool can_uncompute(int node_id, const ScheduleState &state,
                          const PredicateDag &dag) {
  if (!state.live_nodes.count(node_id))
    return false;
  auto it = dag.node_by_id.find(node_id);
  if (it == dag.node_by_id.end())
    return false;
  const AndNode *node = it->second;
  return is_literal_available(node->lhs_lit, state, dag) &&
         is_literal_available(node->rhs_lit, state, dag);
}

} // namespace sched

// abstract eviction policy

class EvictionPolicy {
public:
  virtual ~EvictionPolicy() = default;

  // Choose a victim for eviction.
  // The candidate must be:
  //   - currently live
  //   - not in the protected set
  //   - legally uncomputable (fanins available)
  // return nullopt if no legal candidate exists
  virtual std::optional<int>
  choose_eviction_candidate(const ScheduleState &state,
                            const PredicateDag &dag,
                            const std::unordered_set<int> &protected_set) const = 0;

  virtual const char *name() const = 0;
};
