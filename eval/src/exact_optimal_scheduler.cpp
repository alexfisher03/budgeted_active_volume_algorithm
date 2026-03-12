#include "exact_optimal_scheduler.hpp"
#include "parser.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <vector>

// ============================================================
// compact node indexing
//
// DAG node ids (e.g. 11..26) are mapped to compact bit positions
// [0..N-1] for bitmask operations
// ============================================================

struct CompactIndex {
  std::vector<int> index_to_node; // compact index -> node_id
  std::unordered_map<int, int> node_to_index; // node_id -> compact index
  int N = 0; // number of internal nodes

  // precomputed per-node fanin info in compact index space
  // for each compact index i:
  //   fanin_mask[i] = bitmask of internal fanin nodes
  //   fanin_all_external[i] = true if both fanins are inputs/constants
  std::vector<uint32_t> fanin_mask;
  std::vector<bool> fanin_all_external;

  int root_index = -1; // compact index of the root node
};

static CompactIndex build_compact_index(const PredicateDag &dag) {
  CompactIndex ci;

  // sort node ids for deterministic ordering
  std::vector<int> sorted_ids;
  sorted_ids.reserve(dag.nodes.size());
  for (const auto &n : dag.nodes)
    sorted_ids.push_back(n.id);
  std::sort(sorted_ids.begin(), sorted_ids.end());

  ci.N = static_cast<int>(sorted_ids.size());
  ci.index_to_node.resize(static_cast<std::size_t>(ci.N));
  for (int i = 0; i < ci.N; ++i) {
    ci.index_to_node[static_cast<std::size_t>(i)] = sorted_ids[static_cast<std::size_t>(i)];
    ci.node_to_index[sorted_ids[static_cast<std::size_t>(i)]] = i;
  }

  // find root compact index
  int root_lit = dag.root_lit;
  if (!is_constant_false(root_lit) && !is_constant_true(root_lit)) {
    int root_base = positive_base_literal(root_lit);
    if (!dag.input_lit_set.count(root_base)) {
      int root_nid = node_id_from_positive_literal(root_base);
      auto it = ci.node_to_index.find(root_nid);
      if (it != ci.node_to_index.end())
        ci.root_index = it->second;
    }
  }

  // precompute fanin masks
  ci.fanin_mask.resize(static_cast<std::size_t>(ci.N), 0);
  ci.fanin_all_external.resize(static_cast<std::size_t>(ci.N), true);

  for (int i = 0; i < ci.N; ++i) {
    int nid = ci.index_to_node[static_cast<std::size_t>(i)];
    auto nit = dag.node_by_id.find(nid);
    if (nit == dag.node_by_id.end())
      continue;
    const AndNode *node = nit->second;

    uint32_t mask = 0;
    for (int lit : {node->lhs_lit, node->rhs_lit}) {
      if (is_constant_false(lit) || is_constant_true(lit))
        continue;
      int base = positive_base_literal(lit);
      if (dag.input_lit_set.count(base))
        continue;
      // internal fanin
      int fanin_nid = node_id_from_positive_literal(base);
      auto fit = ci.node_to_index.find(fanin_nid);
      if (fit != ci.node_to_index.end()) {
        mask |= (1u << fit->second);
        ci.fanin_all_external[static_cast<std::size_t>(i)] = false;
      }
    }
    ci.fanin_mask[static_cast<std::size_t>(i)] = mask;
  }

  return ci;
}

// ============================================================
// bitmask helpers
// ============================================================

static inline bool is_live(uint32_t mask, int idx) {
  return (mask & (1u << idx)) != 0;
}

static inline uint32_t set_live(uint32_t mask, int idx) {
  return mask | (1u << idx);
}

static inline uint32_t clear_live(uint32_t mask, int idx) {
  return mask & ~(1u << idx);
}

static inline int popcount32(uint32_t x) { return __builtin_popcount(x); }

// check if all fanins of compact node idx are available under mask
// (inputs/constants are always available so only internal fanins matter)
static inline bool fanins_available(uint32_t mask, int idx,
                                    const CompactIndex &ci) {
  if (ci.fanin_all_external[static_cast<std::size_t>(idx)])
    return true;
  uint32_t needed = ci.fanin_mask[static_cast<std::size_t>(idx)];
  return (mask & needed) == needed;
}

// ============================================================
// state encoding
//
// state = (mask, root_used) packed into uint64_t
// low 32 bits: live-set bitmask
// bit 32: root_used flag
// ============================================================

static inline uint64_t encode_state(uint32_t mask, bool root_used) {
  return static_cast<uint64_t>(mask) |
         (root_used ? (1ULL << 32) : 0ULL);
}

static inline uint32_t decode_mask(uint64_t state) {
  return static_cast<uint32_t>(state & 0xFFFFFFFFULL);
}

static inline bool decode_root_used(uint64_t state) {
  return (state & (1ULL << 32)) != 0;
}

// ============================================================
// edge / action in the configuration graph
// ============================================================

struct CfgEdge {
  ScheduleOpType op;
  int compact_index; // which node was computed/uncomputed (-1 for UseRoot)
  uint32_t cost;     // 1 for compute/uncompute, 0 for UseRoot
};

// ============================================================
// Dijkstra search
// ============================================================

ExactOptimalResult
ExactOptimalScheduler::run(const PredicateDag &dag, std::size_t budget) const {
  ExactOptimalResult result;
  result.budget = budget;

  CompactIndex ci = build_compact_index(dag);

  if (ci.root_index < 0) {
    // root is a constant or input — no schedule needed
    result.feasible = false;
    return result;
  }

  result.root_node_id = ci.index_to_node[static_cast<std::size_t>(ci.root_index)];

  int B = static_cast<int>(budget);

  // start state: empty live-set, root_used = false
  uint64_t start = encode_state(0, false);
  // goal state: empty live-set, root_used = true
  uint64_t goal = encode_state(0, true);

  // dist[state] = best known cost to reach state
  std::unordered_map<uint64_t, uint32_t> dist;
  // pred[state] = (predecessor_state, edge that led here)
  struct PredInfo {
    uint64_t pred_state;
    CfgEdge edge;
  };
  std::unordered_map<uint64_t, PredInfo> pred;

  // min-heap: (cost, state)
  using PQEntry = std::pair<uint32_t, uint64_t>;
  std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

  dist[start] = 0;
  pq.push({0, start});

  std::size_t states_explored = 0;

  while (!pq.empty()) {
    auto [d, u] = pq.top();
    pq.pop();

    // skip stale entries
    auto dit = dist.find(u);
    if (dit != dist.end() && d > dit->second)
      continue;

    states_explored++;

    // goal check
    if (u == goal) {
      // reconstruct the optimal trace
      result.feasible = true;
      result.states_explored = states_explored;

      // walk predecessor chain
      std::vector<CfgEdge> rev_edges;
      uint64_t cur = goal;
      while (cur != start) {
        auto pit = pred.find(cur);
        if (pit == pred.end())
          break;
        rev_edges.push_back(pit->second.edge);
        cur = pit->second.pred_state;
      }
      std::reverse(rev_edges.begin(), rev_edges.end());

      // convert edges to ScheduleActions and compute metrics
      uint32_t cur_mask = 0;
      std::size_t step = 0;
      std::size_t peak_av = 0;
      std::unordered_map<int, int> compute_count;

      for (const auto &e : rev_edges) {
        int node_id = (e.compact_index >= 0)
                          ? ci.index_to_node[static_cast<std::size_t>(e.compact_index)]
                          : result.root_node_id;

        switch (e.op) {
        case ScheduleOpType::Compute:
          cur_mask = set_live(cur_mask, e.compact_index);
          compute_count[node_id]++;
          break;
        case ScheduleOpType::Uncompute:
          cur_mask = clear_live(cur_mask, e.compact_index);
          break;
        case ScheduleOpType::UseRoot:
          break;
        }

        std::size_t live_count = static_cast<std::size_t>(popcount32(cur_mask));
        if (live_count > peak_av)
          peak_av = live_count;

        result.actions.push_back({e.op, node_id, live_count, step});
        step++;
      }

      // derive metrics from the reconstructed trace
      ScheduleMetrics &m = result.metrics;
      m.peak_active_volume = peak_av;
      m.total_compute_ops = 0;
      m.total_uncompute_ops = 0;
      m.total_recomputations = 0;
      m.total_cost = 0;

      for (const auto &a : result.actions) {
        if (a.op == ScheduleOpType::Compute)
          m.total_compute_ops++;
        else if (a.op == ScheduleOpType::Uncompute)
          m.total_uncompute_ops++;
      }

      for (const auto &[nid, cnt] : compute_count) {
        if (cnt > 1)
          m.total_recomputations += static_cast<std::size_t>(cnt - 1);
      }

      // cost = total_compute_ops + total_uncompute_ops (unit cost model)
      m.total_cost = m.total_compute_ops + m.total_uncompute_ops;

      return result;
    }

    uint32_t mask = decode_mask(u);
    bool root_used = decode_root_used(u);

    // generate successors

    // 1. Compute(v) for each non-live node whose fanins are available
    //    and resulting live-set size <= B
    int live_count = popcount32(mask);
    if (live_count < B) {
      for (int i = 0; i < ci.N; ++i) {
        if (is_live(mask, i))
          continue;
        if (!fanins_available(mask, i, ci))
          continue;

        uint32_t new_mask = set_live(mask, i);
        uint64_t v = encode_state(new_mask, root_used);
        uint32_t new_cost = d + 1;

        auto vit = dist.find(v);
        if (vit == dist.end() || new_cost < vit->second) {
          dist[v] = new_cost;
          pred[v] = {u, {ScheduleOpType::Compute, i, 1}};
          pq.push({new_cost, v});
        }
      }
    }

    // 2. Uncompute(v) for each live node whose fanins are available
    for (int i = 0; i < ci.N; ++i) {
      if (!is_live(mask, i))
        continue;
      if (!fanins_available(mask, i, ci))
        continue;

      uint32_t new_mask = clear_live(mask, i);
      uint64_t v = encode_state(new_mask, root_used);
      uint32_t new_cost = d + 1;

      auto vit = dist.find(v);
      if (vit == dist.end() || new_cost < vit->second) {
        dist[v] = new_cost;
        pred[v] = {u, {ScheduleOpType::Uncompute, i, 1}};
        pq.push({new_cost, v});
      }
    }

    // 3. UseRoot if root is live and not yet used
    if (!root_used && is_live(mask, ci.root_index)) {
      uint64_t v = encode_state(mask, true);
      uint32_t new_cost = d; // UseRoot cost = 0

      auto vit = dist.find(v);
      if (vit == dist.end() || new_cost < vit->second) {
        dist[v] = new_cost;
        pred[v] = {u, {ScheduleOpType::UseRoot, ci.root_index, 0}};
        pq.push({new_cost, v});
      }
    }
  }

  // goal was never reached
  result.feasible = false;
  result.states_explored = states_explored;
  return result;
}

// ============================================================
// printing helpers
// ============================================================

static const char *eo_op_name(ScheduleOpType op) {
  switch (op) {
  case ScheduleOpType::Compute:
    return "Compute  ";
  case ScheduleOpType::Uncompute:
    return "Uncompute";
  case ScheduleOpType::UseRoot:
    return "UseRoot  ";
  }
  return "?";
}

void print_exact_optimal_trace(const ExactOptimalResult &result,
                               const PredicateDag &dag) {
  std::cout << "\n-- Configuration --\n";
  std::cout << "  scheduler = exact-optimal\n";
  std::cout << "  budget    = " << result.budget << "\n";
  std::cout << "  result    = "
            << (result.feasible ? "FEASIBLE" : "INFEASIBLE") << "\n";
  std::cout << "  states_explored = " << result.states_explored << "\n";

  if (!result.feasible)
    return;

  // topological order
  std::vector<int> topo;
  for (const auto &n : dag.nodes)
    topo.push_back(n.id);
  std::sort(topo.begin(), topo.end());

  std::cout << "\n-- Topological order (" << topo.size() << " nodes) --\n ";
  for (std::size_t i = 0; i < topo.size(); ++i) {
    if (i > 0)
      std::cout << ", ";
    std::cout << topo[i];
  }
  std::cout << "\n";

  std::cout << "\n-- Root --\n";
  std::cout << "  root_lit=" << dag.root_lit
            << "  root_node_id=" << result.root_node_id << "\n";

  std::cout << "\n-- Schedule trace (" << result.actions.size()
            << " actions) --\n";
  for (const auto &a : result.actions) {
    std::cout << "  [" << a.step << "] " << eo_op_name(a.op) << "  node "
              << a.node_id << "  live_after=" << a.live_count_after << "\n";
  }
}

void print_exact_optimal_metrics(const ExactOptimalResult &result) {
  const auto &m = result.metrics;
  std::cout << "\n-- Schedule metrics --\n";
  std::cout << "  budget               = " << result.budget << "\n";
  std::cout << "  peak_active_volume   = " << m.peak_active_volume << "\n";
  std::cout << "  total_compute_ops    = " << m.total_compute_ops << "\n";
  std::cout << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
  std::cout << "  total_recomputations = " << m.total_recomputations << "\n";
  std::cout << "  total_cost           = " << m.total_cost << "\n";
  std::cout << "  states_explored      = " << result.states_explored << "\n";
}

// ============================================================
// CLI entry points
// ============================================================

int run_exact_optimal_demo(const std::string &json_path, std::size_t budget) {
  std::cout << "Loading predicate from: " << json_path << "\n";
  PredicateDag dag = load_predicate(json_path);
  std::cout << "Loaded " << dag.export_id << " (" << dag.inputs.size()
            << " inputs, " << dag.nodes.size()
            << " nodes, root_lit=" << dag.root_lit << ")\n";

  std::cout << "\n=== EXACT OPTIMAL SCHEDULER, B=" << budget << " ===\n";

  ExactOptimalScheduler scheduler;
  ExactOptimalResult result = scheduler.run(dag, budget);

  print_exact_optimal_trace(result, dag);
  print_exact_optimal_metrics(result);

  std::cout << "\n";
  return result.feasible ? 0 : 1;
}

int run_exact_optimal_budget_list(const std::string &json_path, std::size_t lo,
                                  std::size_t hi,
                                  const std::string &out_path) {
  PredicateDag dag = load_predicate(json_path);

  std::ostream *out = &std::cout;
  std::ofstream file;
  if (!out_path.empty()) {
    file.open(out_path);
    if (!file.is_open()) {
      std::cerr << "Error: cannot open output file: " << out_path << "\n";
      return 1;
    }
    out = &file;
  }

  *out << "# scheduler: exact-optimal\n";
  *out << "# dag: " << dag.export_id << " (" << dag.nodes.size()
       << " internal nodes)\n\n";

  ExactOptimalScheduler scheduler;

  for (std::size_t b = lo; b <= hi; ++b) {
    ExactOptimalResult r = scheduler.run(dag, b);
    const auto &m = r.metrics;

    *out << "B = " << b << "\n";
    if (r.feasible) {
      *out << "  feasible             = yes\n";
      *out << "  peak_active_volume   = " << m.peak_active_volume << "\n";
      *out << "  total_compute_ops    = " << m.total_compute_ops << "\n";
      *out << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
      *out << "  total_recomputations = " << m.total_recomputations << "\n";
      *out << "  total_cost           = " << m.total_cost << "\n";
      *out << "  states_explored      = " << r.states_explored << "\n";
    } else {
      *out << "  feasible             = no\n";
      *out << "  states_explored      = " << r.states_explored << "\n";
    }
    if (b < hi)
      *out << "\n";
  }

  if (!out_path.empty())
    std::cerr << "Wrote " << (hi - lo + 1) << " entries to " << out_path
              << "\n";

  return 0;
}
