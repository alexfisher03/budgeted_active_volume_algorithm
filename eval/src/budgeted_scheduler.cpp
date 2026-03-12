#include "budgeted_scheduler.hpp"
#include "oldest_live_policy.hpp"
#include "parser.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

// scheduling primitives

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

// metrics

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

  // unit-cost model: total_cost = 2 * sum_v compute_count(v)
  std::size_t sum_cost = 0;
  for (const auto &[nid, count] : state.compute_count)
    sum_cost += static_cast<std::size_t>(count);
  m.total_cost = 2 * sum_cost;

  return m;
}

// check whether evicting candidate would strand any currently-uncomputable
// live node, i.e. turn it into a zombie by killing one of its fanins
static bool would_strand_live_node(int candidate,
                                   const ScheduleState &state,
                                   const PredicateDag &dag) {
  for (int w : state.live_nodes) {
    if (w == candidate)
      continue;
    // only care about nodes that are currently uncomputable;
    // if w is already a zombie removing candidate doesnt make it worse
    if (!sched::can_uncompute(w, state, dag))
      continue;
    auto wit = dag.node_by_id.find(w);
    if (wit == dag.node_by_id.end())
      continue;
    const AndNode *wnode = wit->second;
    for (int fl : {wnode->lhs_lit, wnode->rhs_lit}) {
      int fn = sched::producing_node_id_for_literal(fl, dag);
      if (fn == candidate)
        return true;
    }
  }
  return false;
}

// ensure_live

bool BudgetedScheduler::ensure_live(
    int node_id, ScheduleState &state, const PredicateDag &dag,
    std::size_t budget, const EvictionPolicy &policy,
    const std::unordered_set<int> &inherited_protected) const {

  if (state.live_nodes.count(node_id))
    return true;

  auto local_protected = inherited_protected;
  local_protected.insert(node_id);

  auto it = dag.node_by_id.find(node_id);
  if (it == dag.node_by_id.end())
    throw std::runtime_error("ensure_live: unknown node " +
                             std::to_string(node_id));
  const AndNode *node = it->second;

  // recursively ensure internal fanins are live
  for (int fanin_lit : {node->lhs_lit, node->rhs_lit}) {
    int fanin_nid = sched::producing_node_id_for_literal(fanin_lit, dag);
    if (fanin_nid != -1) {
      if (!ensure_live(fanin_nid, state, dag, budget, policy, local_protected))
        return false;
      local_protected.insert(fanin_nid);
    }
  }

  // make room if the live set is at budget
  while (state.live_nodes.size() >= budget) {
    // strand-preferring eviction: try to find a candidate whose removal
    // does not strand a currently-uncomputable live node.  if no such
    // candidate exists fall back to the policys original choice -- creating
    // a zombie is better than declaring infeasible prematurely
    auto eviction_protected = local_protected;
    bool evicted = false;

    // phase 1: try to find a strand-safe candidate
    while (true) {
      auto victim =
          policy.choose_eviction_candidate(state, dag, eviction_protected);
      if (!victim.has_value())
        break;

      if (!would_strand_live_node(*victim, state, dag)) {
        bdo_uncompute(*victim, state, dag);
        evicted = true;
        break;
      }

      // would strand -- exclude it and try the next one
      eviction_protected.insert(*victim);
    }

    // phase 2: no strand-safe candidate, fall back to original policy choice
    if (!evicted) {
      auto victim =
          policy.choose_eviction_candidate(state, dag, local_protected);
      if (!victim.has_value())
        return false;
      bdo_uncompute(*victim, state, dag);
    }
  }

  assert(state.live_nodes.size() < budget &&
         "live set must be below budget before compute");

  bdo_compute(node_id, state, dag);

  assert(state.live_nodes.size() <= budget &&
         "live set exceeded budget after compute");

  return true;
}

// cleanup

bool BudgetedScheduler::cleanup(ScheduleState &state, const PredicateDag &dag,
                                std::size_t budget,
                                const EvictionPolicy &policy) const {
  while (!state.live_nodes.empty()) {
    int v = *state.live_nodes.rbegin();

    if (sched::can_uncompute(v, state, dag)) {
      bdo_uncompute(v, state, dag);
      continue;
    }

    // re-materialize missing fanins so v can be uncomputed
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

    if (!sched::can_uncompute(v, state, dag))
      return false;
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

  std::size_t node_count = dag.nodes.size();

  ScheduleState state;
  state.root_node_id = root_node_id;

  // phase a: ensure root is live
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

  // phase b: use root
  bdo_use_root(state);

  // phase c: cleanup
  if (!cleanup(state, dag, budget, policy)) {
    result.feasible = false;
    result.metrics = compute_metrics(state);
    result.actions = std::move(state.actions);
    return result;
  }

  if (!state.live_nodes.empty())
    throw std::runtime_error(
        "BudgetedScheduler: live set not empty after cleanup (" +
        std::to_string(state.live_nodes.size()) + " nodes remain)");

  // when budget >= |V| there should be no recomputations
  if (budget >= node_count) {
    ScheduleMetrics m = compute_metrics(state);
    assert(m.total_recomputations == 0 &&
           "budget >= |V| but recomputations occurred");
    assert(m.total_compute_ops == node_count &&
           "budget >= |V| but compute_ops != |V|");
  }

  result.feasible = true;
  result.metrics = compute_metrics(state);
  result.actions = std::move(state.actions);
  return result;
}

// printing

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

// runners

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
