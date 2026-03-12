#include "exact_optimal_scheduler.hpp"
#include "parser.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// compact node indexing
//
// dag node ids (e.g. 12..52) are mapped to compact bit positions
// [0..N-1] for bitmask operations

struct CompactIndex {
  std::vector<int> index_to_node;
  std::unordered_map<int, int> node_to_index;
  int N = 0;

  // precomputed per-node fanin info in compact index space
  std::vector<uint64_t> fanin_mask;
  std::vector<bool> fanin_all_external;

  int root_index = -1;
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
  assert(ci.N <= 63 && "more than 63 internal nodes not supported");

  ci.index_to_node.resize(static_cast<std::size_t>(ci.N));
  for (int i = 0; i < ci.N; ++i) {
    ci.index_to_node[static_cast<std::size_t>(i)] =
        sorted_ids[static_cast<std::size_t>(i)];
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
  ci.fanin_mask.resize(static_cast<std::size_t>(ci.N), 0ULL);
  ci.fanin_all_external.resize(static_cast<std::size_t>(ci.N), true);

  for (int i = 0; i < ci.N; ++i) {
    int nid = ci.index_to_node[static_cast<std::size_t>(i)];
    auto nit = dag.node_by_id.find(nid);
    if (nit == dag.node_by_id.end())
      continue;
    const AndNode *node = nit->second;

    uint64_t mask = 0ULL;
    for (int lit : {node->lhs_lit, node->rhs_lit}) {
      if (is_constant_false(lit) || is_constant_true(lit))
        continue;
      int base = positive_base_literal(lit);
      if (dag.input_lit_set.count(base))
        continue;
      int fanin_nid = node_id_from_positive_literal(base);
      auto fit = ci.node_to_index.find(fanin_nid);
      if (fit != ci.node_to_index.end()) {
        mask |= (1ULL << fit->second);
        ci.fanin_all_external[static_cast<std::size_t>(i)] = false;
      }
    }
    ci.fanin_mask[static_cast<std::size_t>(i)] = mask;
  }

  return ci;
}

// bitmask helpers (uint64_t, supports up to 63 nodes)

static inline bool is_live(uint64_t mask, int idx) {
  return (mask & (1ULL << idx)) != 0;
}

static inline uint64_t set_live(uint64_t mask, int idx) {
  return mask | (1ULL << idx);
}

static inline uint64_t clear_live(uint64_t mask, int idx) {
  return mask & ~(1ULL << idx);
}

static inline int popcount64(uint64_t x) { return __builtin_popcountll(x); }

// fanin availability check in compact index space
static inline bool fanins_available(uint64_t mask, int idx,
                                    const CompactIndex &ci) {
  if (ci.fanin_all_external[static_cast<std::size_t>(idx)])
    return true;
  uint64_t needed = ci.fanin_mask[static_cast<std::size_t>(idx)];
  return (mask & needed) == needed;
}

// state encoding
//
// single uint64_t: bits 0..62 = live-set mask, bit 63 = root_used
// supports up to 63 internal nodes

static inline uint64_t encode_state(uint64_t mask, bool root_used) {
  return mask | (root_used ? (1ULL << 63) : 0ULL);
}

static inline uint64_t decode_mask(uint64_t state) {
  return state & 0x7FFFFFFFFFFFFFFFULL;
}

static inline bool decode_root_used(uint64_t state) {
  return (state >> 63) != 0;
}

// edge in the configuration graph

struct CfgEdge {
  ScheduleOpType op;
  int compact_index;
  uint32_t cost;
};

// maximum states explored before bailing out
static constexpr std::size_t MAX_STATES = 50'000'000;

// build the store-all schedule for the B >= N shortcut
// this is provably optimal: cost = 2*N, 0 recomputations
static ExactOptimalResult
build_store_all_result(const PredicateDag & /*dag*/, const CompactIndex &ci,
                       std::size_t budget) {
  ExactOptimalResult result;
  result.feasible = true;
  result.budget = budget;
  result.root_node_id =
      ci.index_to_node[static_cast<std::size_t>(ci.root_index)];
  result.states_explored = 0;

  std::size_t N = static_cast<std::size_t>(ci.N);

  // topological order: compute all nodes in sorted id order
  // then use root, then uncompute all in reverse order
  std::vector<int> topo;
  topo.reserve(N);
  for (int i = 0; i < ci.N; ++i)
    topo.push_back(ci.index_to_node[static_cast<std::size_t>(i)]);

  std::size_t step = 0;

  // compute phase
  for (std::size_t i = 0; i < N; ++i) {
    result.actions.push_back(
        {ScheduleOpType::Compute, topo[i], i + 1, step++});
  }

  // use root
  result.actions.push_back(
      {ScheduleOpType::UseRoot, result.root_node_id, N, step++});

  // uncompute phase (reverse order)
  for (std::size_t i = N; i > 0; --i) {
    result.actions.push_back(
        {ScheduleOpType::Uncompute, topo[i - 1], i - 1, step++});
  }

  auto &m = result.metrics;
  m.peak_active_volume = N;
  m.total_compute_ops = N;
  m.total_uncompute_ops = N;
  m.total_recomputations = 0;
  m.total_cost = 2 * N;
  m.fallback_evictions = 0;

  return result;
}

// dijkstra search over the configuration graph

ExactOptimalResult
ExactOptimalScheduler::run(const PredicateDag &dag, std::size_t budget) const {
  ExactOptimalResult result;
  result.budget = budget;

  CompactIndex ci = build_compact_index(dag);

  if (ci.root_index < 0) {
    result.feasible = false;
    return result;
  }

  result.root_node_id =
      ci.index_to_node[static_cast<std::size_t>(ci.root_index)];

  int N = ci.N;
  int B = static_cast<int>(budget);

  // shortcut: B >= N means store-all is optimal
  if (B >= N)
    return build_store_all_result(dag, ci, budget);

  // start and goal states
  uint64_t start = encode_state(0ULL, false);
  uint64_t goal = encode_state(0ULL, true);

  // dist[state] = best known cost to reach state
  std::unordered_map<uint64_t, uint32_t> dist;
  // pred[state] = predecessor state and edge
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

    // state limit
    if (states_explored > MAX_STATES) {
      result.feasible = false;
      result.states_explored = states_explored;
      return result;
    }

    // goal check
    if (u == goal) {
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

      // replay trace to compute metrics
      uint64_t cur_mask = 0ULL;
      std::size_t step = 0;
      std::size_t peak_av = 0;
      std::unordered_map<int, int> compute_count;

      for (const auto &e : rev_edges) {
        int node_id =
            (e.compact_index >= 0)
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

        std::size_t live_count =
            static_cast<std::size_t>(popcount64(cur_mask));
        if (live_count > peak_av)
          peak_av = live_count;

        result.actions.push_back({e.op, node_id, live_count, step});
        step++;
      }

      // derive metrics
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
          m.total_recomputations +=
              static_cast<std::size_t>(cnt - 1);
      }

      m.total_cost = m.total_compute_ops + m.total_uncompute_ops;

      // lower-bound assertions
      std::size_t uN = static_cast<std::size_t>(N);
      assert(m.total_compute_ops >= uN &&
             "exact-optimal: compute_ops < node count");
      assert(m.total_uncompute_ops >= uN &&
             "exact-optimal: uncompute_ops < node count");
      assert(m.total_cost >= 2 * uN &&
             "exact-optimal: cost < 2 * node count");
      assert(m.peak_active_volume <= budget &&
             "exact-optimal: peak_av exceeded budget");

      // distinct computed nodes must equal N
      std::size_t distinct_computed = compute_count.size();
      assert(distinct_computed == uN &&
             "exact-optimal: not all nodes were computed");

      return result;
    }

    uint64_t mask = decode_mask(u);
    bool root_used = decode_root_used(u);

    // generate successors

    // 1. compute(v) for each non-live node whose fanins are available
    int live_count = popcount64(mask);
    if (live_count < B) {
      for (int i = 0; i < N; ++i) {
        if (is_live(mask, i))
          continue;
        if (!fanins_available(mask, i, ci))
          continue;

        uint64_t new_mask = set_live(mask, i);
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

    // 2. uncompute(v) for each live node whose fanins are available
    for (int i = 0; i < N; ++i) {
      if (!is_live(mask, i))
        continue;
      if (!fanins_available(mask, i, ci))
        continue;

      uint64_t new_mask = clear_live(mask, i);
      uint64_t v = encode_state(new_mask, root_used);
      uint32_t new_cost = d + 1;

      auto vit = dist.find(v);
      if (vit == dist.end() || new_cost < vit->second) {
        dist[v] = new_cost;
        pred[v] = {u, {ScheduleOpType::Uncompute, i, 1}};
        pq.push({new_cost, v});
      }
    }

    // 3. use root if root is live and not yet used
    if (!root_used && is_live(mask, ci.root_index)) {
      uint64_t v = encode_state(mask, true);
      uint32_t new_cost = d;

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

// printing

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

// cli entry points

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
