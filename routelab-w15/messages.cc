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
  return os;
}

RoutingMessage::RoutingMessage()
{}


RoutingMessage::RoutingMessage(const RoutingMessage &rhs)
{}
#endif

#if defined(DISTANCEVECTOR)
ostream &RoutingMessage::Print(ostream &os) const
{

    os << "routing message from router No: " << src;
    for (unsigned i=0; i < dv.size(); ++i) {
        os << dv[i] << ' ';
    }
    os << '\n';
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

