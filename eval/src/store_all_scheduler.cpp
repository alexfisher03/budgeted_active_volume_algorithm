#include "store_all_scheduler.hpp"
#include "parser.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

static bool is_input_or_constant_literal(int lit, const PredicateDag &dag) {
  if (is_constant_false(lit) || is_constant_true(lit))
    return true;
  int base = positive_base_literal(lit);
  return dag.input_lit_set.count(base) > 0;
}

// Returns the internal node id that produces the given literal,
// or -1 if the literal is a constant or primary input.
static int producing_node_id_for_literal(int lit, const PredicateDag &dag) {
  if (is_input_or_constant_literal(lit, dag))
    return -1;
  int base = positive_base_literal(lit);
  int nid = node_id_from_positive_literal(base);
  if (!dag.node_by_id.count(nid))
    throw std::runtime_error("producing_node_id_for_literal: literal " +
                             std::to_string(lit) +
                             " maps to unknown node " + std::to_string(nid));
  return nid;
}

// A literal is available if it is a constant, primary input,
// or produced by an internal node that is currently live.
static bool is_literal_available(int lit, const ScheduleState &state,
                                 const PredicateDag &dag) {
  if (is_input_or_constant_literal(lit, dag))
    return true;
  int nid = producing_node_id_for_literal(lit, dag);
  return state.live_nodes.count(nid) > 0;
}

// legality helpers

static bool can_compute(int node_id, const ScheduleState &state,
                        const PredicateDag &dag) {
  auto it = dag.node_by_id.find(node_id);
  if (it == dag.node_by_id.end())
    return false;
  const AndNode *node = it->second;
  return is_literal_available(node->lhs_lit, state, dag) &&
         is_literal_available(node->rhs_lit, state, dag);
}

static bool can_uncompute(int node_id, const ScheduleState &state,
                          const PredicateDag &dag) {
  if (!state.live_nodes.count(node_id))
    return false;
  auto it = dag.node_by_id.find(node_id);
  if (it == dag.node_by_id.end())
    return false;
  const AndNode *node = it->second;
  return is_literal_available(node->lhs_lit, state, dag) &&
         is_literal_available(node->rhs_lit, state, dag);
}

// scheduling steps

static void do_compute(int node_id, ScheduleState &state,
                       const PredicateDag &dag) {
  if (!can_compute(node_id, state, dag))
    throw std::runtime_error("do_compute: illegal compute for node " +
                             std::to_string(node_id));
  if (state.live_nodes.count(node_id))
    throw std::runtime_error("do_compute: node " + std::to_string(node_id) +
                             " is already live");

  state.live_nodes.insert(node_id);
  state.compute_count[node_id]++;

  std::size_t live_count = state.live_nodes.size();
  if (live_count > state.peak_active_volume)
    state.peak_active_volume = live_count;

  state.actions.push_back(
      {ScheduleOpType::Compute, node_id, live_count, state.actions.size()});
}

static void do_uncompute(int node_id, ScheduleState &state,
                         const PredicateDag &dag) {
  if (!can_uncompute(node_id, state, dag))
    throw std::runtime_error("do_uncompute: illegal uncompute for node " +
                             std::to_string(node_id));

  state.live_nodes.erase(node_id);
  state.uncompute_count[node_id]++;

  std::size_t live_count = state.live_nodes.size();
  state.actions.push_back(
      {ScheduleOpType::Uncompute, node_id, live_count, state.actions.size()});
}

static void do_use_root(ScheduleState &state) {
  if (state.root_used)
    throw std::runtime_error("do_use_root: root already used");
  if (!state.live_nodes.count(state.root_node_id))
    throw std::runtime_error("do_use_root: root node " +
                             std::to_string(state.root_node_id) +
                             " is not live");

  state.root_used = true;
  std::size_t live_count = state.live_nodes.size();
  state.actions.push_back({ScheduleOpType::UseRoot, state.root_node_id,
                           live_count, state.actions.size()});
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

  // Unit-cost model: total_cost = 2 * sum_v tau(v)(1 + r_pi(v))
  // treating tau(v) = 1 for this experiment
  // r_pi(v) = compute_count(v) - 1
  std::size_t sum_cost = 0;
  for (const auto &[nid, count] : state.compute_count)
    sum_cost += static_cast<std::size_t>(count);
  m.total_cost = 2 * sum_cost;

  return m;
}

// store-all algorithm

ScheduleResult StoreAllScheduler::run(const PredicateDag &dag) const {
  // determine root node id from root_lit
  int root_lit = dag.root_lit;
  if (is_constant_false(root_lit) || is_constant_true(root_lit))
    throw std::runtime_error(
        "StoreAllScheduler: root_lit is a constant; no internal node to use");
  int root_base = positive_base_literal(root_lit);
  if (dag.input_lit_set.count(root_base))
    throw std::runtime_error(
        "StoreAllScheduler: root_lit is an input; no internal node to use");
  int root_node_id = node_id_from_positive_literal(root_base);
  if (!dag.node_by_id.count(root_node_id))
    throw std::runtime_error("StoreAllScheduler: root maps to unknown node " +
                             std::to_string(root_node_id));

  // build topological order (ascending node id) 
  std::vector<int> topo_order;
  topo_order.reserve(dag.nodes.size());
  for (const auto &n : dag.nodes)
    topo_order.push_back(n.id);
  std::sort(topo_order.begin(), topo_order.end());

  // init state
  ScheduleState state;
  state.root_node_id = root_node_id;

  // Phase A: forward pass (compute each internal node exactly once)
  for (int nid : topo_order)
    do_compute(nid, state, dag);

  // Phase B: mark root as used
  do_use_root(state);

  // Phase C: cleanup (uncompute in reverse topological order)
  for (auto it = topo_order.rbegin(); it != topo_order.rend(); ++it)
    do_uncompute(*it, state, dag);

  // post-condition checks
  if (!state.live_nodes.empty())
    throw std::runtime_error(
        "StoreAllScheduler: live set not empty after cleanup (" +
        std::to_string(state.live_nodes.size()) + " nodes remain)");
  if (!state.root_used)
    throw std::runtime_error("StoreAllScheduler: root was never used");

  // build result (compute metrics before moving actions)
  ScheduleResult result;
  result.root_node_id = root_node_id;
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

void print_schedule_trace(const ScheduleResult &result,
                          const PredicateDag &dag) {
  // print topological order
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

  // print each action
  std::cout << "\n-- Schedule trace (" << result.actions.size()
            << " actions) --\n";
  for (const auto &a : result.actions) {
    std::cout << "  [" << a.step << "] " << op_name(a.op) << "  node "
              << a.node_id << "  live_after=" << a.live_count_after << "\n";
  }
}

void print_schedule_metrics(const ScheduleMetrics &m) {
  std::cout << "\n-- Schedule metrics --\n";
  std::cout << "  peak_active_volume   = " << m.peak_active_volume << "\n";
  std::cout << "  total_compute_ops    = " << m.total_compute_ops << "\n";
  std::cout << "  total_uncompute_ops  = " << m.total_uncompute_ops << "\n";
  std::cout << "  total_recomputations = " << m.total_recomputations << "\n";
  std::cout << "  total_cost           = " << m.total_cost << "\n";
}

// demo runner

int run_store_all_demo(const std::string &json_path) {
  std::cout << "Loading predicate from: " << json_path << "\n";
  PredicateDag dag = load_predicate(json_path);
  std::cout << "Loaded " << dag.export_id << " (" << dag.inputs.size()
            << " inputs, " << dag.nodes.size()
            << " nodes, root_lit=" << dag.root_lit << ")\n";

  std::cout << "\n=== STORE-ALL SCHEDULER ===\n";

  StoreAllScheduler scheduler;
  ScheduleResult result = scheduler.run(dag);

  print_schedule_trace(result, dag);
  print_schedule_metrics(result.metrics);

  std::cout << "\n";
  return 0;
}
