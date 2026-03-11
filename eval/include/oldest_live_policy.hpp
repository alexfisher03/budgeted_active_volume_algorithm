#pragma once

#include "eviction_policy.hpp"

// Evicts the live, non-protected, legally uncomputable node
// with the smallest live_since_step timestamp (oldest).
class OldestLivePolicy : public EvictionPolicy {
public:
  std::optional<int>
  choose_eviction_candidate(const ScheduleState &state,
                            const PredicateDag &dag,
                            const std::unordered_set<int> &protected_set) const override;

  const char *name() const override { return "oldest-live"; }
};
