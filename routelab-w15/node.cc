#include "node.h"
#include "context.h"
#include "error.h"

#if defined(DISTANCEVECTOR)
#define MAX_NODES 256
#endif

Node::Node(const unsigned n, SimulationContext *c, double b, double l) :
    number(n), context(c), bw(b), lat(l)
{
    #if defined(DISTANCEVECTOR)
    route_table = Table(n, MAX_NODES);
    #endif
}

Node::Node()
{ throw GeneralException(); }

Node::Node(const Node &rhs) :
  number(rhs.number), context(rhs.context), bw(rhs.bw), lat(rhs.lat)
{
    #if defined(DISTANCEVECTOR)
    route_table = Table(rhs.route_table);
    #endif
}

Node & Node::operator=(const Node &rhs)
{
  return *(new(this)Node(rhs));
}

void Node::SetNumber(const unsigned n)
{
    number=n;
    #if defined(DISTANCEVECTOR)
    cerr << "FOR DISTANCE VECTOR number is unique assigned and can not be changed\n";
    throw GeneralException();
    #endif
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
    cerr << "send to neighbors not implemented \n";
}

void Node::SendToNeighbor(const Node *n, const RoutingMessage *m)
{
    cerr << "send to neighbors not implemented \n";
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


void Node::LinkHasBeenUpdated(const Link *l)
{
  cerr << *this<<": Link Update: "<<*l<<endl;
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
  cerr << *this << " Routing Message: "<<*m;
}

void Node::TimeOut()
{
  cerr << *this << " got a timeout: ignored"<<endl;
}

// compliance with DV algorithm
// Node *Node::GetNextHop(const Node *destination) const
Node *Node::GetNextHop(const Node *destination)
{
  if (table.rt.count(destination)) {
    return table.rt[destination];
  } else {
    err << "[ERROR] GetNextHop: Unknown Node\n";
    return nullptr;
  }
}

Table *Node::GetRoutingTable() const
{
  return &table;
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
    cerr << *this<<": Link Update: "<<*l<<endl;
    bool changed = route_table.update_neighbour(l->GetDest(), l->GetLatency());
    if (changed) {
        RoutingMessage msg(number, route_table.get_my_dv());
        SendToNeighbors(&msg);
    }
}


void Node::ProcessIncomingRoutingMessage(const RoutingMessage *m)
{
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
