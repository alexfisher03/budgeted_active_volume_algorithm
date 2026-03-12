#pragma once

#include "dag_analysis.hpp"
#include "eviction_policy.hpp"

// smallest fanout policy

// among live, non-protected, legally uncomputable candidates,
// choose the node with the smallest fanout count

// a node with fewer downstream dependents is less
// likely to be needed again soon, so evicting it causes fewer future
// recomputations

// deterministic tie-break: smallest node id wins

class SmallestFanoutPolicy : public EvictionPolicy {
public:
  explicit SmallestFanoutPolicy(const DagAnalysis &analysis)
      : analysis_(analysis) {}

  std::optional<int>
  choose_eviction_candidate(const ScheduleState &state,
                            const PredicateDag &dag,
                            const std::unordered_set<int> &protected_set) const override;

  const char *name() const override { return "smallest-fanout"; }

private:
  const DagAnalysis &analysis_;
};
