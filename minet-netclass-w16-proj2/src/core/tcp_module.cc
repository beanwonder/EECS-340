#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <iostream>

#include "Minet.h"
#include "tcpstate.h"


using std::cout;
using std::endl;
using std::cerr;
using std::string;

int main(int argc, char *argv[])
{
    MinetHandle mux, sock;

    MinetInit(MINET_TCP_MODULE);

    ConnectionList<TCPState> clist;

    mux=MinetIsModuleInConfig(MINET_IP_MUX) ? MinetConnect(MINET_IP_MUX) : MINET_NOHANDLE;
    sock=MinetIsModuleInConfig(MINET_SOCK_MODULE) ? MinetAccept(MINET_SOCK_MODULE) : MINET_NOHANDLE;

    if (MinetIsModuleInConfig(MINET_IP_MUX) && mux==MINET_NOHANDLE) {
        MinetSendToMonitor(MinetMonitoringEvent("Can't connect to mux"));
        return -1;
    }

    if (MinetIsModuleInConfig(MINET_SOCK_MODULE) && sock==MINET_NOHANDLE) {
        MinetSendToMonitor(MinetMonitoringEvent("Can't accept from sock module"));
        return -1;
    }

    MinetSendToMonitor(MinetMonitoringEvent("tcp_module handling TCP traffic"));
    cerr << "tcp_module handling TCP traffic......\n";

    MinetEvent event;

    while (MinetGetNextEvent(event)==0) {
        // if we received an unexpected type of event, print error
        if (event.eventtype!=MinetEvent::Dataflow || event.direction!=MinetEvent::IN) {
          MinetSendToMonitor(MinetMonitoringEvent("Unknown event ignored."));
          cerr << "Invalid event from Minet\n" << endl;
        } else {
          //  Data from the IP layer below  //

            if (event.handle==mux) {
                Packet p;
                MinetReceive(mux,p);
                unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(p);
                cerr << "estimated header len=" << tcphlen << "\n";
                p.ExtractHeaderFromPayload<TCPHeader>(tcphlen);
                IPHeader iph = p.FindHeader(Headers::IPHeader);
                TCPHeader tcph = p.FindHeader(Headers::TCPHeader);
                Connection c;

                // cerr << "IP Header is "<<iph<<"\n";
                // cerr << "TCP Header is "<<tcph << "\n";
                if (!tcph.IsCorrectChecksum(p)) {
                    cerr << "Checksum check FAILED!!!\n";
                    //continue;
                }

                // Identifty the connection with 5-tuple
                iph.GetDestIP(c.src);
                iph.GetSourceIP(c.dest);
                iph.GetProtocol(c.protocol);
                tcph.GetDestPort(c.srcport);
                tcph.GetSourcePort(c.destport);

                auto cs = clist.FindMatching(c);
                cerr << (*cs).state << '\n';
                cerr << (*cs).connection << '\n';

                if(cs == clist.end()) {
                    cerr << "ERROR: invalid connection detected!!!\n";
                    continue;
                } 

                
                SockRequestResponse resp;
                cerr << "INFO:  identified connection\n";

                switch ((*cs).state.GetState())
                {
                    case eState::LISTEN:
                        cout << "Passive open ...\n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if (IS_SYN(flags) && !IS_ACK(flags)) { // TODO what if IS_RST


                            // Build SYN Segment
                            // TODO have a make package function later
                            cout << "Build packet...\n";
                            Packet p;
                            IPHeader ih;
                            ih.SetProtocol(IP_PROTO_TCP);
                            ih.SetSourceIP(c.dest);
                            ih.SetDestIP(c.src);
                            ih.SetTotalLength(TCP_HEADER_BASE_LENGTH+IP_HEADER_BASE_LENGTH);
                            p.PushFrontHeader(ih);
                            // build tcp header
                            cout << "TCP head...\n";
                            TCPHeader th;
                            unsigned int seqnum;
                            seqnum = 0xF0F0F0F0; // should be random later
                            th.SetSeqNum(seqnum, p); 
                            unsigned int acknum;
                            tcph.GetSeqNum(acknum);
                            th.SetAckNum(acknum + 1, p);
                            th.SetSourcePort(c.destport, p);
                            th.SetDestPort(c.srcport, p);
                            th.SetHeaderLen(TCP_HEADER_BASE_LENGTH, p);
                            unsigned char f = 0;
                            SET_SYN(f);
                            SET_ACK(f);
                            th.SetFlags(f, p);
                            cout << "before push\n";
                            p.PushBackHeader(th);
                            cout << "befor minet send... \n";
                            MinetSend(mux, p);

                            (cs->state).SetState(eState::SYN_RCVD);
                            cerr << "SYN ACK packet sent\n";

                            // Create a new connection
                            cout << "New connection...\n";
                            auto nts = TCPState(seqnum, SYN_RCVD, 10);
                            auto nc  = ConnectionToStateMapping<TCPState> (c, Time(), nts, false);
                            clist.push_front(nc);

                            resp.type = STATUS;
                            resp.connection = c;
                            resp.bytes = 0;
                            resp.error = EOK;
                            MinetSend(sock, resp);
                            MinetSendToMonitor(MinetMonitoringEvent("SYN ACK SNET"));
                        }
                        break;
                }
            }
            //  Data from the Sockets layer above  //
            if (event.handle==sock) {
                SockRequestResponse s;
                MinetReceive(sock,s);
                cerr << "Received Socket Request:" << s << endl;
                SockRequestResponse resp;

                switch (s.type) {
                    case CONNECT: {

                    }
                    case ACCEPT: {
                        auto cs = clist.FindMatching(s.connection);
                        if (cs == clist.end()) {
                            // TODO int seqnum sould be random
                            // NJJ: Use a special initial pattern here for debugging,
                            // it should be a 32-bit counter that increments by one every 4 microseconds
                            TCPState tstate(0xAAAA5555, LISTEN, 1000); // TODO 1000?
                            auto con = ConnectionToStateMapping<TCPState>(s.connection, Time(), tstate, false); // TODO false?
                            clist.push_back(con);
                        }
                        resp.type = STATUS;
                        // resp.connection = s.connection;
                        // resp.bytes = 0;
                        resp.error = EOK;
                        MinetSend(sock, resp);
                        MinetSendToMonitor(MinetMonitoringEvent("LISTEN CREATED"));
                        cerr << "Listen Created on connection: \n" << s.connection << "\n";
                    }
                    case WRITE: {

                    }
                    case FORWARD: {

                    }
                    case CLOSE: {

                    }
                    case STATUS: {

                    }
                }
            }
        }
    }
    return 0;
}
