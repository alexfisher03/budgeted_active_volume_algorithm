#pragma once

#include "dag_analysis.hpp"
#include "eviction_policy.hpp"

// depth-aware policy

// among live, non-protected, legally uncomputable candidates,
// choose the node with the smallest structural depth

// smaller depth  =>  closer to primary inputs  =>  cheaper to
// rebuild from available inputs if evicted, because fewer
// recursive ensure_live calls are needed to re-materialize it

// deterministic tie-break: smallest node id wins

class DepthAwarePolicy : public EvictionPolicy {
public:
  explicit DepthAwarePolicy(const DagAnalysis &analysis)
      : analysis_(analysis) {}

  std::optional<int>
  choose_eviction_candidate(const ScheduleState &state,
                            const PredicateDag &dag,
                            const std::unordered_set<int> &protected_set) const override;

  const char *name() const override { return "depth-aware"; }

private:
  const DagAnalysis &analysis_;
};
