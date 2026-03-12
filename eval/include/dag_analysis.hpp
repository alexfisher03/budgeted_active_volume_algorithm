#pragma once

#include "predicate_dag.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

// precomputed structural analysis of a PredicateDag
// These quantities are static properties of the DAG topology
// and do not change during scheduling, compute once and pass
// to policies that need them

struct DagAnalysis {
  // fanout_count[node_id] = number of AND nodes that use node_id's output
  // literal as a fanin
  // Nodes with small fanout are "less depended on" and cheaper to evict
  // because fewer downstream nodes will need them re-materialized
  std::unordered_map<int, int> fanout_count;

  // depth[node_id] = length of the longest path from any primary input to this node
  // Inputs have depth 0
  // a node whose both fanins are inputs has depth 1.
  //
  // Smaller depth  =>  closer to inputs  =>  cheaper to rebuild from
  // available inputs if evicted (fewer recursive ensure_live calls)
  std::unordered_map<int, int> depth;
};

// build the analysis from the PredicateDag
// Requires that dag.build_lookups() has already been called
inline DagAnalysis build_dag_analysis(const PredicateDag &dag) {
  DagAnalysis a;

  // fanout counts 
  // initialize every node's fanout to 0
  for (const auto &n : dag.nodes)
    a.fanout_count[n.id] = 0;

  // for each AND node, increment the fanout of each internal fanin
  for (const auto &n : dag.nodes) {
    for (int lit : {n.lhs_lit, n.rhs_lit}) {
      int base = positive_base_literal(lit);
      // skip constants and primary inputs
      if (is_constant_false(lit) || is_constant_true(lit))
        continue;
      if (dag.input_lit_set.count(base))
        continue;
      int fanin_id = node_id_from_positive_literal(base);
      if (a.fanout_count.count(fanin_id))
        a.fanout_count[fanin_id]++;
    }
  }

  // depth 
  // topological order is ascending node id
  // process nodes in that order so that fanin depths are available
  // before the node that uses them
  std::vector<int> topo;
  topo.reserve(dag.nodes.size());
  for (const auto &n : dag.nodes)
    topo.push_back(n.id);
  std::sort(topo.begin(), topo.end());

  for (int nid : topo) {
    auto it = dag.node_by_id.find(nid);
    if (it == dag.node_by_id.end())
      continue;
    const AndNode *node = it->second;

    int d = 0; // depth of this node
    for (int lit : {node->lhs_lit, node->rhs_lit}) {
      int base = positive_base_literal(lit);
      if (is_constant_false(lit) || is_constant_true(lit))
        continue;
      if (dag.input_lit_set.count(base))
        continue;
      int fanin_id = node_id_from_positive_literal(base);
      auto dit = a.depth.find(fanin_id);
      if (dit != a.depth.end())
        d = std::max(d, dit->second + 1);
      else
        d = std::max(d, 1); // should not happen if topo is correct
    }
    a.depth[nid] = d;
  }

  return a;
}
