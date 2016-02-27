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

int makePacket(Packet& p, Buffer& data, Connection& c, unsigned char flags, unsigned int segNum, unsigned int ackNum);
int timeoutHandler();

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
                cout << "\nEvent from mux received\n";
                Packet p;
                MinetReceive(mux,p);
                unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(p);
                cerr << "Estimated TCP header len=" << tcphlen << "\n";
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

                if(cs == clist.end()) {
                    cerr << "ERROR: invalid connection detected!!!\n";
                    continue;
                }


                SockRequestResponse resp;
                cerr << tcph << "\n";
                cerr << c << "\n";

                switch ((*cs).state.GetState())
                {
                    case eState::LISTEN:
                        cout << "Passive open ...\n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if (IS_SYN(flags) && !IS_ACK(flags)) { // TODO what if IS_RST
                            /*
                            // Build SYN Segment
                            // TODO have a make package function later
                            cout << "Building response packet...\n";
                            Packet p;
                            IPHeader ih;
                            ih.SetProtocol(IP_PROTO_TCP);
                            ih.SetSourceIP(c.src);
                            ih.SetDestIP(c.dest);
                            ih.SetTotalLength(TCP_HEADER_BASE_LENGTH + IP_HEADER_BASE_LENGTH);
                            ih.SetID((unsigned short)rand());
                            p.PushFrontHeader(ih);
                            // build tcp header
                            TCPHeader th;
                            unsigned int seqnum;
                            seqnum = 12345678; // should be random later
                            th.SetSeqNum(seqnum, p);
                            unsigned int acknum;
                            tcph.GetSeqNum(acknum);
                            th.SetAckNum(acknum + 1, p);
                            th.SetSourcePort(c.srcport, p);
                            th.SetDestPort(c.destport, p);
                            th.SetHeaderLen(TCP_HEADER_BASE_LENGTH / 4, p);
                            th.SetWinSize(14600, p);
                            unsigned char f = 0;
                            SET_SYN(f);
                            SET_ACK(f);
                            th.SetFlags(f, p);
                            p.PushBackHeader(th);
                            MinetSend(mux, p);
                            */
                            Packet p;
                            Buffer data(NULL, 0);
                            unsigned char flags = 0;
                            unsigned int ack_num;
                            // unsigned int seq_num;
                            tcph.GetSeqNum(ack_num);
                            ack_num += 1;

                            makePacket(p, data, c, flags, 0, 0, ack_num);

                            (cs->state).SetState(eState::SYN_RCVD);
                            cerr << "SYN ACK packet sent\n";

                            // Create a new connection
                            cout << "Createing new connection...\n";
                            cout << p << "\n";
                            cout << (IPHeader)p.FindHeader(Headers::IPHeader) << "\n";
                            cout << (TCPHeader)p.FindHeader(Headers::TCPHeader) << "\n";
                            auto nts = TCPState(seqnum, SYN_RCVD, 10);
                            auto nc  = ConnectionToStateMapping<TCPState> (c, Time(), nts, false);
                            clist.push_front(nc);

                            resp.type = STATUS;
                            resp.connection = c;
                            resp.bytes = 0;
                            resp.error = EOK;
                            MinetSend(sock, resp);
                            MinetSendToMonitor(MinetMonitoringEvent("SYN ACK SENT"));
                        }
                        break;

                    case eState::SYN_RCVD:
                        {
                            cout << "SYN_RCVD: \n";
                        }
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
                        cerr << "Listening on connection: \n" << s.connection << "\n";
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

int makePacket(Packet& p, Buffer& data, Connection& c,
               unsigned char flags, unsigned int win_size,
               unsigned int seq_num = 0, unsigned int ack_num = 0)
{

    // TODO data ? how to use it
    unsigned int num_bytes = MAX_MACRO(IP_PACKET_MAX_LENGTH + TCP_HEADER_BASE_LENGTH, data.GetSize());
    // Packet p(data.ExtractFront(num_bytes));
    p = Packet(data.ExtractFront(num_bytes));

    // header
    // IP header
    //
    IPheader iph();
    iph.SetDestIP(c.src);
    iph.SetDestIP(c.dest);
    iph.SetProtocol(c.protocol);
    iph.SetTotalLength(IP_PACKET_MAX_LENGTH + TCP_HEADER_BASE_LENGTH + num_bytes);
    p.PushFrontHeader(iph);
    // TCPHeader

    TCPheader tcph();
    tcph.SetSourcePort(c.srcport, p);
    tcph.SetDestPort(c.destport, p);

    if (seq_num == 0) {
        seq_num = (unsigned int) rand();
    }

    tcph.SetSeqNum(seq_num, p);
    tcph.SetAckNum(ack_num, p);

    tcph.SetHeaderLen(TCP_HEADER_BASE_LENGTH / 4, p);

    tcph.SetFlags(flags, p);
    tcph.SetWinSize(win_size, p);
    tcph.PushBackHeader(tcph);

    return 0;
}
