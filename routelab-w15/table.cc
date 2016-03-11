#include "table.h"

#if defined(GENERIC)
ostream & Table::Print(ostream &os) const
{
  // WRITE THIS
  os << "Table()";
  return os;
}
#endif

#if defined(LINKSTATE)

#include <cassert>

Table::Table() {}

Table::Table(const Table &rhs)
  : g(rhs.g), rt(rhs.rt) {}

bool Table::have_next_hop(unsigned node_id)
{
  return rt.count(node_id) == 1;
}

unsigned Table::get_next_hop(unsigned node_id)
{
  return rt[node_id];
}

ostream &Table::Print(ostream &os) const
{
  os << "\n[INFO] Table\n";
  //os << "route table: " << rt << "\n";
}

#endif

#if defined(DISTANCEVECTOR)
#include <cmath>
#include <cassert>
#include <algorithm>
#include <limits>
// #include "link.h"
#include "error.h"

Table::Table() {}

Table::Table(unsigned num, unsigned num_nodes) {
    number = num;
    this->num_nodes = num_nodes;
    dv_table = vector<vector<double>>(num_nodes, vector<double>(num_nodes, std::numeric_limits<double>::infinity()));
    // dv_table 0 0 should not be considered
    for (unsigned i = 0; i < num_nodes; i++) {
        dv_table[i][i] = 0;
    }
    // neighbours = nbrs;
    direct_cost = std::vector<double> (num_nodes, std::numeric_limits<double>::infinity());
    // direct_cost[number] = 0;
    // unreachable when next hop point to my self<-> a loop
    next_hop = std::vector<unsigned> (num_nodes, number);
}

Table::Table(const Table &rhs) : number(rhs.number), num_nodes(rhs.num_nodes),
                                 dv_table(rhs.dv_table), direct_cost(rhs.direct_cost),
                                 next_hop(rhs.next_hop) {}

ostream& Table::Print(ostream &os) const {
    os << "distance vector:\n";
    for (size_t i = 0; i < dv_table.size(); i++) {
        cout << i << ":    ";
        for (size_t j = 0; j < dv_table[i].size(); ++j) {
            os << dv_table[i][j] << "    ";
        }
        os << '\n';
    }

    os << "direct cost:\n";
    for (size_t i = 0; i < direct_cost.size(); i++) {
        os << direct_cost[i] << ' ';
    }
    os << '\n';

    os << "next hop:\n";
    for (size_t i = 0; i < next_hop.size(); i++) {
        os << next_hop[i] << ' ';
    }
    os << '\n';
    return os;
}

bool Table::update_table_with_dv(unsigned num, vector<double> dv) {
    // assumption that id must be a neighbor
    //
    // get distance to that neighbour
    // int d_x_v = dv_table[node_id][id];
    // update table
    assert(num != number);
    dv_table[num] = dv;

    return recompute_table();
    /*
    for (size_t i = 0; i < num_nodes; i++) {
        if (dv[i] != dv_table[num][i]) {
            // changed then update
            dv_table[num][i] = dv[i];
            // update dv_table[number][i]
            double min_dis = std::numeric_limits<double>::infinity();
            // suppose all nodes are 0 - num_nodes
            // unreachable @nhop = -1
            int nhop = -1;
            for (int idx=0; idx < num_nodes; ++idx) {
                // neighbours
                int d_xv = direct_cost[idx];
                if (!isinf(d_xv)) {
                    // is neighbours
                    if (d_xv + dv_table[idx][i] < min_dis) {
                        min_dis = d_xv + table[idx][i];
                        nhop = idx;
                    }
                }
            }
            // update my own dv_table
            dv_table[number][i] = min_dis;
            next_hop[i] = nhop;
        }
    }
    */
}

/*
bool Table::update_neighbours(deque<Link*> lks) {
    for (deque<Link*>::iterator it = lks.begin(); it != lks.end(); ++it) {
        if ((*it)->GetSrc() == number) {
            direct_cost[(*it)->GetDest()] = (*it)->GetLatency();
        }
    }
    return recompute_table();
}
*/

bool Table::update_neighbour(unsigned num, double direct_dis) {
    // this function do not check
    // update the direct distance
    assert(num != number);
    direct_cost[num] = direct_dis;
    return recompute_table();
}

bool Table::recompute_table() {
    // based on current info recompute the routing table infos
    bool flag = false;
    for (unsigned i = 0; i < num_nodes; i++) {
        if (i == number) {
            continue;
        }

        double min_dis = std::numeric_limits<double>::infinity();
        // nhop == i no where next jump
        unsigned nhop = number;
        for (unsigned idx = 0; idx < num_nodes; idx++) {
            if (idx == number) {
                continue;
            }
            double d_xv = direct_cost[idx];
            if (!isinf(d_xv)) {
                // is neighbour
                if (d_xv + dv_table[idx][i] < min_dis) {
                    min_dis = d_xv + dv_table[idx][i];
                    nhop = idx;
                }
            }
        }

        if (dv_table[number][i] != min_dis) {
            flag = true;
        }
        dv_table[number][i] = min_dis;
        next_hop[i] = nhop;
    }

    return flag;
}

unsigned Table::get_next_hop(unsigned n) const {
    // return my self id when unreachable
    return next_hop[n];
}

vector<double> Table::get_my_dv() const {
    return dv_table[number];
}

#endif
