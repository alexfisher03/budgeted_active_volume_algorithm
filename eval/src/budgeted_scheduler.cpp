#include "budgeted_scheduler.hpp"
#include "oldest_live_policy.hpp"
#include "parser.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

// scheduling steps (budgeted versions with live_since_step)

static void bdo_compute(int node_id, ScheduleState &state,
                        const PredicateDag &dag) {
  if (!sched::can_compute(node_id, state, dag))
    throw std::runtime_error("bdo_compute: illegal compute for node " +
                             std::to_string(node_id));
  if (state.live_nodes.count(node_id))
    throw std::runtime_error("bdo_compute: node " + std::to_string(node_id) +
                             " is already live");

  std::size_t step = state.actions.size();
  state.live_nodes.insert(node_id);
  state.compute_count[node_id]++;
  state.live_since_step[node_id] = step;

  std::size_t live_count = state.live_nodes.size();
  if (live_count > state.peak_active_volume)
    state.peak_active_volume = live_count;

  state.actions.push_back(
      {ScheduleOpType::Compute, node_id, live_count, step});
}

static void bdo_uncompute(int node_id, ScheduleState &state,
                          const PredicateDag &dag) {
  if (!sched::can_uncompute(node_id, state, dag))
    throw std::runtime_error("bdo_uncompute: illegal uncompute for node " +
                             std::to_string(node_id));

  state.live_nodes.erase(node_id);
  state.uncompute_count[node_id]++;
  state.live_since_step.erase(node_id);

  std::size_t live_count = state.live_nodes.size();
  std::size_t step = state.actions.size();
  state.actions.push_back(
      {ScheduleOpType::Uncompute, node_id, live_count, step});
}

static void bdo_use_root(ScheduleState &state) {
  if (state.root_used)
    throw std::runtime_error("bdo_use_root: root already used");
  if (!state.live_nodes.count(state.root_node_id))
    throw std::runtime_error("bdo_use_root: root node " +
                             std::to_string(state.root_node_id) +
                             " is not live");

  state.root_used = true;
  std::size_t live_count = state.live_nodes.size();
  std::size_t step = state.actions.size();
  state.actions.push_back(
      {ScheduleOpType::UseRoot, state.root_node_id, live_count, step});
}

// metrics computation

static ScheduleMetrics compute_metrics(const ScheduleState &state) {
  ScheduleMetrics m;
  m.peak_active_volume = state.peak_active_volume;

  m.total_compute_ops = 0;
  for (const auto &[nid, count] : state.compute_count)
    m.total_compute_ops += static_cast<std::size_t>(count);

  m.total_uncompute_ops = 0;
  for (const auto &[nid, count] : state.uncompute_count)
    m.total_uncompute_ops += static_cast<std::size_t>(count);

  m.total_recomputations = 0;
  for (const auto &[nid, count] : state.compute_count) {
    if (count > 1)
      m.total_recomputations += static_cast<std::size_t>(count - 1);
  }

  // Unit-cost model: total_cost = 2 * sum_v compute_count(v)
  // treating tau(v) = 1 for this experiment
  std::size_t sum_cost = 0;
  for (const auto &[nid, count] : state.compute_count)
    sum_cost += static_cast<std::size_t>(count);
  m.total_cost = 2 * sum_cost;

  return m;
}

// ensure_live

bool BudgetedScheduler::ensure_live(
    int node_id, ScheduleState &state, const PredicateDag &dag,
    std::size_t budget, const EvictionPolicy &policy,
    const std::unordered_set<int> &inherited_protected) const {

  if (state.live_nodes.count(node_id))
    return true;

  // Build a local protected set.  Starts with whatever the caller needs
  // protected, plus this node (so it cannot be evicted before it is
  // computed, but prevents the eviction policy from choosing it in degenerate cases)
  auto local_protected = inherited_protected; // copy
  local_protected.insert(node_id);

  // Recursively ensure internal fanins are live.
  // After each fanin is ensured live, add it to local_protected so that
  // the sibling fanin call and the eviction loop below cannot evict it.
  // crucially, the local copy means nodes added deeper in the recursion
  // don't accumulate into this level's protected set
  auto it = dag.node_by_id.find(node_id);
  if (it == dag.node_by_id.end())
    throw std::runtime_error("ensure_live: unknown node " +
                             std::to_string(node_id));
  const AndNode *node = it->second;

  for (int fanin_lit : {node->lhs_lit, node->rhs_lit}) {
    int fanin_nid = sched::producing_node_id_for_literal(fanin_lit, dag);
    if (fanin_nid != -1) {
      if (!ensure_live(fanin_nid, state, dag, budget, policy, local_protected))
        return false;
      // protect the just-ensured fanin from eviction by sibling calls
      // and by the eviction loop below
      local_protected.insert(fanin_nid);
    }
  }

  // make room if the live set is at budget
  while (state.live_nodes.size() >= budget) {
    auto victim =
        policy.choose_eviction_candidate(state, dag, local_protected);
    if (!victim.has_value())
      return false; // infeasible under this policy / budget
    bdo_uncompute(*victim, state, dag);
  }

  // compute the node
  bdo_compute(node_id, state, dag);
  return true;
}

// cleanup

bool BudgetedScheduler::cleanup(ScheduleState &state, const PredicateDag &dag,
                                std::size_t budget,
                                const EvictionPolicy &policy) const {
  // repeatedly pick the highest-id live node and uncompute it
  // if its fanins are unavailable, re-materialize them first
  // the highest live id strictly decreases each iteration

  while (!state.live_nodes.empty()) {
    int v = *state.live_nodes.rbegin(); // highest-id live node

    // if we can directly uncompute, do so and continue
    if (sched::can_uncompute(v, state, dag)) {
      bdo_uncompute(v, state, dag);
      continue;
    }

    // otherwise, re-materialize missing fanins
    auto nit = dag.node_by_id.find(v);
    if (nit == dag.node_by_id.end())
      throw std::runtime_error("cleanup: unknown node " + std::to_string(v));
    const AndNode *node = nit->second;

    std::unordered_set<int> protected_set;
    protected_set.insert(v);

    for (int fanin_lit : {node->lhs_lit, node->rhs_lit}) {
      int w = sched::producing_node_id_for_literal(fanin_lit, dag);
      if (w != -1 && !state.live_nodes.count(w)) {
        if (!ensure_live(w, state, dag, budget, policy, protected_set))
          return false;
        protected_set.insert(w);
      }
    }

    // now the fanins should be available
    if (!sched::can_uncompute(v, state, dag))
      return false; // should not happen if ensure_live succeeded
    bdo_uncompute(v, state, dag);
  }

  return true;
}

// main entry point

BudgetedScheduleResult
BudgetedScheduler::run(const PredicateDag &dag, std::size_t budget,
                       const EvictionPolicy &policy) const {
  BudgetedScheduleResult result;
  result.budget = budget;
  result.policy_name = policy.name();

  // determine root node id
  int root_lit = dag.root_lit;
  if (is_constant_false(root_lit) || is_constant_true(root_lit)) {
    result.feasible = false;
    return result;
  }
  int root_base = positive_base_literal(root_lit);
  if (dag.input_lit_set.count(root_base)) {
    result.feasible = false;
    return result;
  }
  int root_node_id = node_id_from_positive_literal(root_base);
  if (!dag.node_by_id.count(root_node_id)) {
    result.feasible = false;
    return result;
  }
  result.root_node_id = root_node_id;

  // initialize state
  ScheduleState state;
  state.root_node_id = root_node_id;

  // phase A: ensure root is live (recursively materializes ancestors)
  {
    std::unordered_set<int> protected_set;
    if (!ensure_live(root_node_id, state, dag, budget, policy,
                     protected_set)) {
      result.feasible = false;
      result.metrics = compute_metrics(state);
      result.actions = std::move(state.actions);
      return result;
    }
  }

  // phase B: use root
  bdo_use_root(state);

  // phase C: cleanup — empty the live set
  if (!cleanup(state, dag, budget, policy)) {
    result.feasible = false;
    result.metrics = compute_metrics(state);
    result.actions = std::move(state.actions);
    return result;
  }

  // post-condition checks
  if (!state.live_nodes.empty())
    throw std::runtime_error(
        "BudgetedScheduler: live set not empty after cleanup (" +
        std::to_string(state.live_nodes.size()) + " nodes remain)");

  result.feasible = true;
  result.metrics = compute_metrics(state);
  result.actions = std::move(state.actions);
  return result;
}

// printing helpers

static const char *op_name(ScheduleOpType op) {
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

void print_budgeted_schedule_trace(const BudgetedScheduleResult &result,
                                   const PredicateDag &dag) {
  std::cout << "\n-- Configuration --\n";
  std::cout << "  policy = " << result.policy_name << "\n";
  std::cout << "  budget = " << result.budget << "\n";
  std::cout << "  result = " << (result.feasible ? "FEASIBLE" : "INFEASIBLE")
            << "\n";

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

  // action trace
  std::cout << "\n-- Schedule trace (" << result.actions.size()
            << " actions) --\n";
  for (const auto &a : result.actions) {
    std::cout << "  [" << a.step << "] " << op_name(a.op) << "  node "
              << a.node_id << "  live_after=" << a.live_count_after << "\n";
  }
}

void print_budgeted_schedule_metrics(const BudgetedScheduleResult &result) {
  const auto &m = result.metrics;
  std::cout << "\n-- Schedule metrics --\n";
  std::cout << "  budget               = " << result.budget << "\n";
  std::cout << "  peak_active_volume   = " << m.peak_active_volume << "\n";
  std::cout << "  total_compute_ops    = " << m.total_compute_ops << "\n";
  std::cout << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
  std::cout << "  total_recomputations = " << m.total_recomputations << "\n";
  std::cout << "  total_cost           = " << m.total_cost << "\n";
}

// demo runner

int run_oldest_live_demo(const std::string &json_path, std::size_t budget) {
  std::cout << "Loading predicate from: " << json_path << "\n";
  PredicateDag dag = load_predicate(json_path);
  std::cout << "Loaded " << dag.export_id << " (" << dag.inputs.size()
            << " inputs, " << dag.nodes.size()
            << " nodes, root_lit=" << dag.root_lit << ")\n";

  std::cout << "\n=== BUDGETED SCHEDULER: oldest-live, B=" << budget
            << " ===\n";

  OldestLivePolicy policy;
  BudgetedScheduler scheduler;
  BudgetedScheduleResult result = scheduler.run(dag, budget, policy);

  print_budgeted_schedule_trace(result, dag);
  print_budgeted_schedule_metrics(result);

  std::cout << "\n";
  return result.feasible ? 0 : 1;
}

// budget-list sweep

int run_budget_list(const std::string &json_path, std::size_t lo,
                    std::size_t hi, const std::string &out_path) {
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

  *out << "# policy: oldest-live\n";
  *out << "# dag: " << dag.export_id << " (" << dag.nodes.size()
       << " internal nodes)\n\n";

  OldestLivePolicy policy;
  BudgetedScheduler scheduler;

  for (std::size_t b = lo; b <= hi; ++b) {
    BudgetedScheduleResult r = scheduler.run(dag, b, policy);
    const auto &m = r.metrics;
    *out << "B = " << b << "\n";
    *out << "  feasible             = " << (r.feasible ? "yes" : "no") << "\n";
    *out << "  peak_active_volume   = " << m.peak_active_volume << "\n";
    *out << "  total_compute_ops    = " << m.total_compute_ops << "\n";
    *out << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
    *out << "  total_recomputations = " << m.total_recomputations << "\n";
    *out << "  total_cost           = " << m.total_cost << "\n";
    if (b < hi)
      *out << "\n";
  }

  if (!out_path.empty())
    std::cerr << "Wrote " << (hi - lo + 1) << " rows to " << out_path << "\n";

  return 0;
}