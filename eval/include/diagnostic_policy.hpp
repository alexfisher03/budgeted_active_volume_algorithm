#pragma once

#include "dag_analysis.hpp"
#include "eviction_policy.hpp"

#include <cstddef>
#include <iostream>

// diagnostic wrapper around any EvictionPolicy
//
// at each eviction decision point, enumerates all legal candidates,
// computes all four scoring criteria (fanout, depth, recompute-aware
// score, live_since_step), logs the full candidate table and the
// chosen victim, then delegates to the wrapped policy
//
// this is pure observation — it does not change the scheduling
// engine or any policy's eviction choice

class DiagnosticPolicy : public EvictionPolicy {
public:
  DiagnosticPolicy(const EvictionPolicy &inner, const DagAnalysis &analysis,
                   std::ostream &log)
      : inner_(inner), analysis_(analysis), log_(log) {}

  std::optional<int> choose_eviction_candidate(
      const ScheduleState &state, const PredicateDag &dag,
      const std::unordered_set<int> &protected_set) const override;

  const char *name() const override { return inner_.name(); }

  // how many eviction decisions were made during the run
  std::size_t eviction_count() const { return eviction_count_; }

private:
  const EvictionPolicy &inner_;
  const DagAnalysis &analysis_;
  std::ostream &log_;
  mutable std::size_t eviction_count_ = 0;
};
