#pragma once

#include "dag_analysis.hpp"
#include "eviction_policy.hpp"

// recompute-aware policy
//
// among live, non-protected, legally uncomputable candidates,
// choose the node that minimizes expected "recomputation pain"
// using a deterministic score formula based on precomputed
// structural data and runtime state
//
//   score(v) = depth(v) * fanout(v) * (1 + compute_count(v))
//
// each factor captures one dimension of eviction cost:
//   depth(v)          — how expensive it is to rebuild v from inputs.
//                       a deeper node requires more recursive
//                       ensure_live calls during re-materialization
//
//   fanout(v)         — how many downstream nodes depend on v.
//                       a higher-fanout node is more likely to be
//                       needed again, causing future recomputations
//
//   (1 + compute_count(v)) — how many times v has already been
//                       computed in this schedule
//                       nodes that have already been recomputed multiple times are
//                       likely "hot" and will be needed yet again,
//                       so evicting them incurs even more cost
//                       the +1 ensures the factor is always >= 1
//
// A lower score means the node is cheaper to evict.
// The policy picks the candidate with the minimum score
//
// deterministic tie-break: smallest node id wins

class RecomputeAwarePolicy : public EvictionPolicy {
public:
  explicit RecomputeAwarePolicy(const DagAnalysis &analysis)
      : analysis_(analysis) {}

  std::optional<int>
  choose_eviction_candidate(const ScheduleState &state,
                            const PredicateDag &dag,
                            const std::unordered_set<int> &protected_set) const override;

  const char *name() const override { return "recompute-aware"; }

private:
  const DagAnalysis &analysis_;
};
