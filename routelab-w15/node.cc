#include "node.h"
#include "context.h"
#include "error.h"

#if defined(DISTANCEVECTOR)
#define MAX_NODES 4
#endif

Node::Node(const unsigned n, SimulationContext *c, double b, double l) :
    number(n), context(c), bw(b), lat(l)
{
    #if defined(DISTANCEVECTOR)
    route_table = Table(n, MAX_NODES);
    #endif

    #if defined(LINKSTATE)
    route_table = Table(n);
    #endif
}

Node::Node()
{ throw GeneralException(); }

Node::Node(const Node &rhs) :
  number(rhs.number), context(rhs.context), bw(rhs.bw), lat(rhs.lat)
{
    route_table = Table(rhs.route_table);
}

Node & Node::operator=(const Node &rhs)
{
  return *(new(this)Node(rhs));
}

void Node::SetNumber(const unsigned n)
{
    number=n;
    cerr << "number is unique assigned and can not be changed\n";
    throw GeneralException();
}
unsigned Node::GetNumber() const
{ return number;}

void Node::SetLatency(const double l)
{ lat=l;}

double Node::GetLatency() const
{ return lat;}

void Node::SetBW(const double b)
{ bw=b;}

double Node::GetBW() const
{ return bw;}

Node::~Node()
{}

// Implement these functions  to post an event to the event queue in the event simulator
// so that the corresponding node can recieve the ROUTING_MESSAGE_ARRIVAL event at the proper time
void Node::SendToNeighbors(const RoutingMessage *m)
{
  deque<Node *> * neighbors = GetNeighbors();
  deque<Node *>::iterator iter;
  for (iter = neighbors->begin(); iter != neighbors->end(); ++iter) {
    SendToNeighbor(*iter, new RoutingMessage(*m));
  }
  //context->SendToNeighbors(this, m);
}

void Node::SendToNeighbor(const Node *n, const RoutingMessage *m)
{
  deque<Link *> * links = context->GetOutgoingLinks(this);
  deque<Link *>::iterator iter;
  Link link_pattern;
  link_pattern.SetSrc(number);
  link_pattern.SetDest(n->number);
  for (iter = links->begin(); iter != links->end(); ++iter) {
    if ((*iter)->Matches(link_pattern)) {
      Event * e = new Event (context->GetTime() + (*iter)->GetLatency(),
                             ROUTING_MESSAGE_ARRIVAL,
                             (void *) n,
                             (void *) new RoutingMessage(*m));
      context->PostEvent(e);
    }
  }
  //context->SendToNeighbor(this, n, m);
}

deque<Node*> *Node::GetNeighbors()
{
  return context->GetNeighbors(this);
}

void Node::SetTimeOut(const double timefromnow)
{
  context->TimeOut(this,timefromnow);
}


bool Node::Matches(const Node &rhs) const
{
  return number==rhs.number;
}


#if defined(GENERIC)
void Node::LinkHasBeenUpdated(const Link *l)
{
  cerr << *this << " got a link update: "<<*l<<endl;
  //Do Something generic:
  SendToNeighbors(new RoutingMessage);
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " got a routing messagee: "<<*m<<" Ignored "<<endl;
}


void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

Node *Node::GetNextHop(const Node *destination)
{
  return 0;
}

Table *Node::GetRoutingTable() const
{
  return new Table;
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}

#endif

#if defined(LINKSTATE)
#include <cassert>

void Node::LinkHasBeenUpdated(const Link *l)
{
  cerr << *this<<": Link Update: "<<*l<<endl;
  // check src
  assert(l->GetSrc() == number);
  assert(route_table.g.count(l->GetSrc()) == 1);
  // check dest
  // new node
  if (route_table.g.count(l->GetDest()) == 0) {
    assert(route_table.g[l->GetSrc()].count(l->GetDest()) == 0);
    cout << "New node: " << l->GetDest() << "discovered.\n";
    route_table.g[l->GetDest()];
    assert(route_table.rt.count(l->GetDest()) == 0);
    //route_table.rt[l->GetDest()];
  }
  // link
  if (route_table.g[l->GetSrc()].count(l->GetDest()) == 0) {
    cout << "New link: " << *l << "discovered.\n";
  } else {
    cout << "Link Update: " << *l << "\n";
  }
  Table::Record r(l->GetSrc(), l->GetDest(), l->GetBW(), l->GetLatency());
  route_table.g[l->GetSrc()][l->GetDest()] = r;
  cout << route_table << "\n";
  // send Routing Messge to nerghbors
  unsigned seq_num = route_table.g[l->GetSrc()][l->GetDest()].seq + 1;
  RoutingMessage *message = new RoutingMessage(number, seq_num, *l);
  SendToNeighbors(message);
  route_table.g[l->GetSrc()][l->GetDest()].seq = seq_num;
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " Routing Message: "<<*m;
  //cout << route_table << "\n";
  if (route_table.g.count(m->link.GetSrc()) == 0) {
    cout << "New node: " << m->link.GetSrc() << "discovered.\n";
    route_table.g[m->link.GetSrc()];
    assert(route_table.rt.count(m->link.GetSrc()) == 0);
    //route_table.rt[m->link.GetDest()];
  }
  if (route_table.g.count(m->link.GetDest()) == 0) {
    cout << "New node: " << m->link.GetDest() << "discovered.\n";
    route_table.g[m->link.GetDest()];
    assert(route_table.rt.count(m->link.GetDest()) == 0);
    //route_table.rt[m->link.GetDest()];
  }
  if (route_table.g[m->link.GetSrc()].count(m->link.GetDest()) == 0) {
    cout << "New link: " << m->link << "discovered.\n";
    Table::Record r(m->link.GetSrc(), m->link.GetDest(), m->link.GetBW(), m->link.GetLatency());
    route_table.g[m->link.GetSrc()][m->link.GetDest()] = r;
    SendToNeighbors(m);
    route_table.g[m->link.GetSrc()][m->link.GetDest()].seq = m->seq;
  } else {
    if (route_table.g[m->link.GetSrc()][m->link.GetDest()].seq == m->seq) {
      cout << "Discarding duplicated routing message...";
    } else {
      cout << "Link update: " << m->link << "\n";
      Table::Record r(m->link.GetSrc(), m->link.GetDest(), m->link.GetBW(), m->link.GetLatency());
      route_table.g[m->link.GetSrc()][m->link.GetDest()] = r;
      SendToNeighbors(m);
      route_table.g[m->link.GetSrc()][m->link.GetDest()].seq = m->seq;
    }
  }
  cout << route_table << "\n";
  return;
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

// compliance with DV algorithm
// Node *Node::GetNextHop(const Node *destination) const
Node *Node::GetNextHop(const Node *destination)
{
  assert(route_table.have_next_hop(destination->GetNumber()));
  unsigned next_id = route_table.get_next_hop(destination->GetNumber());
  //assert(next_id != number);
  Node * next_node = NULL;

  deque<Node *> *neighbors = GetNeighbors();
  for (deque<Node *>::iterator it = neighbors->begin();
       it != neighbors->end(); ++it) {
    if ((*it)->number == next_id) {
      cout << "[GetNextHop] from: " << number << " to: " << (*it)->number << "\n";
      return next_node = new Node(*(*it));
    }
  }
  assert(false);
  return NULL;
}

Table *Node::GetRoutingTable() const
{
  return new Table(route_table);
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw<<")";
  return os;
}
#endif


#if defined(DISTANCEVECTOR)

void Node::LinkHasBeenUpdated(const Link *l)
{
    // update our table
    // send out routing mesages
    cerr << *this << ": Link Update: " << *l << '\n';
    bool changed = route_table.update_neighbour(l->GetDest(), l->GetLatency());
    if (changed) {
        RoutingMessage msg(number, route_table.get_my_dv());
        SendToNeighbors(&msg);
    }
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
    cerr << *this << ": RtMsg: " << *m << '\n';
    bool changed = route_table.update_table_with_dv(m->src, m->dv);
    if (changed) {
        RoutingMessage msg(number, route_table.get_my_dv());
        SendToNeighbors(&msg);
    }
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}


Node *Node::GetNextHop(const Node *destination)
{
    unsigned next = route_table.get_next_hop(destination->GetNumber());
    if (next != number) {
        auto neibrs = this->GetNeighbors();
        for (deque<Node*>::iterator it=neibrs->begin(); it != neibrs->end(); ++it) {
            if ((*it)->GetNumber() == next) {
                return new Node(*(*it));
            }
        }
        // if not find that's a mistake throws a exception
        cerr << "no matching for next hop for: \n";
        cerr << route_table;
        throw GeneralException();
    } else {
        // next hop is my self !!!
        // unreachable
        return NULL;
    }
}

Table *Node::GetRoutingTable() const
{
    return new Table(this->route_table);
}


ostream & Node::Print(ostream &os) const
{
  os << "Node(number="<<number<<", lat="<<lat<<", bw="<<bw;
  return os;
}
#endif
