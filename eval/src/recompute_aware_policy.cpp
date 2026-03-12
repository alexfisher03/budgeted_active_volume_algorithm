#include "recompute_aware_policy.hpp"

#include <climits>
#include <cstdint>

std::optional<int> RecomputeAwarePolicy::choose_eviction_candidate(
    const ScheduleState &state, const PredicateDag &dag,
    const std::unordered_set<int> &protected_set) const {

  std::optional<int> best;
  int64_t best_score = INT64_MAX;
  int best_id = INT_MAX;

  for (int nid : state.live_nodes) {
    if (protected_set.count(nid))
      continue;
    if (!sched::can_uncompute(nid, state, dag))
      continue;

    // score(v) = depth(v) * fanout(v) * (1 + compute_count(v)) 

    // lower score = cheaper to evict
    // any zero factor ( a node at depth 0 or with fanout 0)
    // collapses the product to 0, making that node maximally cheap
    // to evict
    // a depth-0 node is trivially rebuilt from inputs, and a fanout-0 node 
    // is not depended on by any other internal node

    auto d_it = analysis_.depth.find(nid);
    int depth = (d_it != analysis_.depth.end()) ? d_it->second : 0;

    auto f_it = analysis_.fanout_count.find(nid);
    int fanout = (f_it != analysis_.fanout_count.end()) ? f_it->second : 0;

    auto c_it = state.compute_count.find(nid);
    int comp = (c_it != state.compute_count.end()) ? c_it->second : 0;

    int64_t score =
        static_cast<int64_t>(depth) * fanout * (1 + comp);

    // smallest score (cheapest to evict)
    // tie-break: smallest node id
    if (!best.has_value() || score < best_score ||
        (score == best_score && nid < best_id)) {
      best_score = score;
      best_id = nid;
      best = nid;
    }
  }

  return best;
}
