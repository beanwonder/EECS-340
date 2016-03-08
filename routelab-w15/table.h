#ifndef _table
#define _table



#include <iostream>
#include <map>

using namespace std;

#if defined(GENERIC)
class Table {
  // Students should write this class

 public:
  ostream & Print(ostream &os) const;
};
#endif


#if defined(LINKSTATE)
class Topology;
class Node;
#include "topology.h"

class Table {
 private:
  // stores graph topology
  map<Node *, map<Node *, Link>> g;
  // sequence-number-controlled flooding
  map<Link, size_t> seq;

 public:
  void update_route_table(Link l);
  ostream & Print(ostream &os) const;
};
#endif

#if defined(DISTANCEVECTOR)

#include <deque>

class Table {
 public:
  ostream & Print(ostream &os) const;
};
#endif

inline ostream & operator<<(ostream &os, const Table &t) { return t.Print(os);}

#endif
