#include "diagnostic_policy.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

std::optional<int> DiagnosticPolicy::choose_eviction_candidate(
    const ScheduleState &state, const PredicateDag &dag,
    const std::unordered_set<int> &protected_set) const {

  ++eviction_count_;

  // print live set (sorted for readability)
  std::vector<int> live_sorted(state.live_nodes.begin(),
                               state.live_nodes.end());
  std::sort(live_sorted.begin(), live_sorted.end());

  std::vector<int> prot_sorted(protected_set.begin(), protected_set.end());
  std::sort(prot_sorted.begin(), prot_sorted.end());

  log_ << "    EVICTION #" << eviction_count_ << " (policy: " << inner_.name()
       << ")\n";

  log_ << "      live_set      = {";
  for (std::size_t i = 0; i < live_sorted.size(); ++i) {
    if (i > 0)
      log_ << ", ";
    log_ << live_sorted[i];
  }
  log_ << "}  (" << live_sorted.size() << " nodes)\n";

  log_ << "      protected_set = {";
  for (std::size_t i = 0; i < prot_sorted.size(); ++i) {
    if (i > 0)
      log_ << ", ";
    log_ << prot_sorted[i];
  }
  log_ << "}  (" << prot_sorted.size() << " nodes)\n";

  // enumerate legal candidates and compute all score types
  struct CandidateInfo {
    int node_id;
    int fanout;
    int depth;
    int compute_count;
    std::size_t live_since_step;
    int64_t recomp_score; // depth * fanout * (1 + compute_count)
    bool is_protected;
    bool can_uncompute;
  };

  std::vector<CandidateInfo> all_live;
  std::vector<CandidateInfo> legal_candidates;

  for (int nid : live_sorted) {
    CandidateInfo c;
    c.node_id = nid;
    c.is_protected = protected_set.count(nid) > 0;
    c.can_uncompute = sched::can_uncompute(nid, state, dag);

    auto f_it = analysis_.fanout_count.find(nid);
    c.fanout = (f_it != analysis_.fanout_count.end()) ? f_it->second : 0;

    auto d_it = analysis_.depth.find(nid);
    c.depth = (d_it != analysis_.depth.end()) ? d_it->second : 0;

    auto cc_it = state.compute_count.find(nid);
    c.compute_count = (cc_it != state.compute_count.end()) ? cc_it->second : 0;

    auto ls_it = state.live_since_step.find(nid);
    c.live_since_step =
        (ls_it != state.live_since_step.end()) ? ls_it->second : 0;

    c.recomp_score =
        static_cast<int64_t>(c.depth) * c.fanout * (1 + c.compute_count);

    all_live.push_back(c);
    if (!c.is_protected && c.can_uncompute)
      legal_candidates.push_back(c);
  }

  // print candidate table
  if (legal_candidates.empty()) {
    log_ << "      candidates: NONE (all live nodes are protected or cannot "
            "be uncomputed)\n";
  } else {
    log_ << "      candidates (" << legal_candidates.size() << " legal):\n";
    log_ << "        node  fanout  depth  comp_cnt  live_since  "
            "recomp_score\n";
    for (const auto &c : legal_candidates) {
      log_ << "        " << std::setw(4) << c.node_id << "  " << std::setw(6)
           << c.fanout << "  " << std::setw(5) << c.depth << "  "
           << std::setw(8) << c.compute_count << "  " << std::setw(10)
           << c.live_since_step << "  " << std::setw(12) << c.recomp_score
           << "\n";
    }
  }

  // also print excluded nodes (protected or non-uncomputable) for visibility
  if (legal_candidates.size() < all_live.size()) {
    log_ << "      excluded:\n";
    for (const auto &c : all_live) {
      if (c.is_protected || !c.can_uncompute) {
        log_ << "        node " << c.node_id;
        if (c.is_protected)
          log_ << " [protected]";
        if (!c.can_uncompute)
          log_ << " [cannot uncompute]";
        log_ << "\n";
      }
    }
  }

  // delegate to the real policy
  auto chosen = inner_.choose_eviction_candidate(state, dag, protected_set);

  if (chosen.has_value()) {
    log_ << "      >>> chosen: node " << *chosen << "\n\n";
  } else {
    log_ << "      >>> chosen: NONE (infeasible)\n\n";
  }

  return chosen;
}
