#ifndef _table
#define _table


#include <iostream>
#include <vector>
#include <deque>
#include <map>

// #include "messages.h"

using namespace std;

#if defined(GENERIC)
class Table {
  // Students should write this class
 public:
  ostream & Print(ostream &os) const;
};
#endif

#if defined(LINKSTATE)
#include "link.h"

class Table {
 private:
  // stores graph topology
  map<unsigned, map<unsigned, Link> > g;
  // sequence-number-controlled flooding
  map<Link, unsigned> seq;
  // route table
  map<unsigned, unsigned> rt;

 public:
  Table();
  Table(const Table &rhs);
  // add or update link info stored in g
  // as well as the route table
  // will broadcast this link update info
  void update_route_table(Link l);

  //
  bool have_next_hop(unsigned node_id);
  unsigned get_next_hop(unsigned node_id);
  ostream & Print(ostream &os) const;
};
#endif

#if defined(DISTANCEVECTOR)
class Table {
    private:
        // indexed by number
        //
        // map<int, double> cost_to_neighbor;
        // vector<double> idv;
        unsigned number;
        unsigned num_nodes;
        vector<vector<double>> dv_table;
        vector<double> direct_cost;
        vector<unsigned> next_hop;
        // deque<Node*> neighbours;
        // map<int, vector<double>> neighbor_dvs;
        bool recompute_table();
    public:
        Table();
        Table(unsigned num, unsigned num_nodes);
        Table(const Table &rhs);
        ostream & Print(ostream &os) const;

        bool update_table_with_dv(unsigned src, vector<double> dv);
        bool update_neighbour(unsigned n, double d);
        // bool update_neighbours(deque<Link*> lks);

        unsigned get_next_hop(unsigned n) const;
        vector<double> get_my_dv() const;
};
#endif

inline ostream & operator<<(ostream &os, const Table &t) { return t.Print(os);}

#endif
