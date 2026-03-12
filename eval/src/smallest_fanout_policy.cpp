#include "smallest_fanout_policy.hpp"

#include <climits>

std::optional<int> SmallestFanoutPolicy::choose_eviction_candidate(
    const ScheduleState &state, const PredicateDag &dag,
    const std::unordered_set<int> &protected_set) const {

  std::optional<int> best;
  int best_fanout = INT_MAX;
  int best_id = INT_MAX;

  for (int nid : state.live_nodes) {
    if (protected_set.count(nid))
      continue;
    if (!sched::can_uncompute(nid, state, dag))
      continue;

    // look up precomputed fanout count, default to 0 if missing
    auto it = analysis_.fanout_count.find(nid);
    int fanout = (it != analysis_.fanout_count.end()) ? it->second : 0;

    // smallest fanout wins
    // tie-break: smallest node id
    if (!best.has_value() || fanout < best_fanout ||
        (fanout == best_fanout && nid < best_id)) {
      best_fanout = fanout;
      best_id = nid;
      best = nid;
    }
  }

  return best;
}
