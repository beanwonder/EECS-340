#include "messages.h"
#include "error.h"

#if defined(GENERIC)
ostream &RoutingMessage::Print(ostream &os) const
{
  os << "RoutingMessage()";
  return os;
}
#endif


#if defined(LINKSTATE)
ostream &RoutingMessage::Print(ostream &os) const
{
  os << "\n[INFO] Routing Message\n";
  os << "sender: " << sender << "\n";
  os << "seqence number: " << seq << "\n";
  os << "link: " << link << "\n";
  return os;
}

RoutingMessage::RoutingMessage()
{}

RoutingMessage::RoutingMessage(unsigned sender, unsigned seq, Link link)
  : sender(sender), seq(seq), link(link) { }

RoutingMessage::RoutingMessage(const RoutingMessage &rhs)
  : sender(rhs.sender), seq(rhs.seq), link(rhs.link) { }
#endif

#if defined(DISTANCEVECTOR)
ostream &RoutingMessage::Print(ostream &os) const
{

    os << "routing message from " << src << ": ";
    for (unsigned i=0; i < dv.size(); ++i) {
        os << dv[i] << ' ';
    }
    return os;
}

RoutingMessage::RoutingMessage() {
    cerr << "RoutingMessage: can not initialize with empty arg\n";
    throw GeneralException();
};

RoutingMessage::RoutingMessage(unsigned src, vector<double> dv)
{
    this->src = src;
    this->dv = vector<double>(dv);
}


RoutingMessage::RoutingMessage(const RoutingMessage &rhs)
{
    src = rhs.src;
    dv = vector<double>(rhs.dv);
}

#endif

