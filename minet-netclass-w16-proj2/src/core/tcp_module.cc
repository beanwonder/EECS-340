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

#define NUM_RETRIES 3
#define WIN_SIZE    1 // limited by the size of N in TCPState

int makePacket(Packet& p, Buffer& data, Connection& c, unsigned char flags,
               unsigned int winSize, unsigned int segNum, unsigned int ackNum = 0);

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
                    case LISTEN:
                    {
                        cout << "Passive open ...\n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if (IS_SYN(flags) && !IS_ACK(flags)) { // TODO what if IS_RST
                            // Build SYN Segment
                            Packet p;
                            Buffer data(NULL, 0);
                            unsigned char flags = 0;
                            SET_SYN(flags);
                            SET_ACK(flags);
                            unsigned int ack_num;
                            unsigned int seq_num = (unsigned int)rand();
                            tcph.GetSeqNum(ack_num);
                            ack_num += 1;

                            makePacket(p, data, c, flags, WIN_SIZE, seq_num, ack_num);

                            int send_status = MinetSend(mux, p);

                            if (send_status == 0) {
                                cerr << "SYN ACK packet sent\n";

                                // Create a new connection
                                cout << "Createing new connection...\n";
                                cout << p << "\n";
                                cout << (IPHeader)p.FindHeader(Headers::IPHeader) << "\n";
                                cout << (TCPHeader)p.FindHeader(Headers::TCPHeader) << "\n";
                                auto nts = TCPState(seq_num, SYN_RCVD, 10);
                                auto nc  = ConnectionToStateMapping<TCPState> (c, Time(), nts, false);
                                clist.push_front(nc);
                                MinetSendToMonitor(MinetMonitoringEvent("SYN ACK SENT"));
                            } else {
                                cerr << "SYN ACK SENT FIALED\n";
                            }

                            // do we need it?
                            // resp.type = STATUS;
                            // resp.connection = c;
                            // resp.bytes = 0;
                            // resp.error = EOK;
                            // MinetSend(sock, resp);
                        }
                        break;
                    }
                    case SYN_RCVD:
                    {
                        cout << "SYN_RCVD: \n";
                        unsigned char recv_flags;
                        tcph.GetFlags(recv_flags);
                        if (!IS_SYN(recv_flags) && IS_ACK(recv_flags)) {
                            cout << "state SYN_RCVD -> ESTABLISHED \n";
                            cs->state.SetState(ESTABLISHED);
                        } else if (IS_RST(recv_flags)) {
                            clist.erase(cs);
                            cout << "reset tcp connection to listen state\n";
                        } else {
                            cerr << "SYN_RCVD: discard all other packet\nt";
                        }
                    }
                    case ESTABLISHED:
                    {
                        cout << "@ ESTABLISHED: \n";
                        unsigned char recv_flags;
                        tcph.GetFlags(recv_flags);

                        if (IS_FIN(recv_flags)) {
                            cs->state.SetState(CLOSE_WAIT);
                            cout << "close wait \n";
                        } else {

                        }
                        break;
                    }
                    case eState::SYN_SENT:
                    {
                        cout << "Active open ack...\n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if(IS_SYN(flags) && IS_ACK(flags)) {
                            // implementment ack piggybacked on data segment later
                            Packet p;
                            Buffer data(NULL, 0);
                            unsigned char flags = 0;
                            SET_ACK(flags);
                            unsigned int recSeqNum;
                            tcph.GetSeqNum(recSeqNum);
                            unsigned int recAckNum;
                            tcph.GetAckNum(recAckNum);
                            TCPState &ts = (*cs).state;

                            // Sanity Check
                            if (recAckNum != ts.last_acked + 1) {
                                cerr << "[ERROR] invalid ack number received: " << recAckNum << "\n";
                                cerr << "        expecting: " << ts.last_acked + 1 << "\n";
                                continue;
                            }

                            // Update TCP State
                            ts.last_recvd = recSeqNum; // + data size

                            makePacket(p, data, c, flags, WIN_SIZE, ts.last_sent, recSeqNum + 1);
                            MinetSend(mux, p);
                            cout << "SYN ACK pakcet sent\n";

                            // Update connection state
                            (*cs).state.SetState(ESTABLISHED);

                        } else {
                            cerr << "[ERROR] Invalud flags: " << flags << "\n";
                            continue;
                        }
                        break;
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
                    case CONNECT:
                    {
                        auto cs = clist.FindMatching(s.connection);
                        if (cs == clist.end()) {
                            // Send the SYN packet
                            Packet p;
                            unsigned char f = 0;
                            SET_SYN(f);
                            Buffer b(NULL, 0);
                            unsigned int seqNum = (unsigned int) rand();
                            makePacket(p, b, s.connection, f, WIN_SIZE, seqNum);

                            int send_status = MinetSend(mux, p);
                            if (send_status == 0) {
                                cout << "SYN packet sent\n";
                                // Create a closed connection
                                // TCPHeader th = (TCPHeader)p.FindHeader(Headers::TCPHeader);
                                // unsigned int seqNum;
                                // th.GetSeqNum(seqNum);
                                TCPState  ts(seqNum, CLOSED, NUM_RETRIES);
                                auto con = ConnectionToStateMapping<TCPState>(s.connection, Time(), ts, false);
                                clist.push_front(con);
                            } else {
                                cerr << "[]ERROR] SYN Packet not send\n";
                            }
                        } else {
                            cerr << "[ERROR] CONNECT: Following connection is already in use:\n" << s.connection << "\n";
                        }
                        break;
                    }

                    case ACCEPT: {
                        auto cs = clist.FindMatching(s.connection);
                        if (cs == clist.end()) {
                            // TODO int seqnum sould be random
                            // NJJ: Use a special initial pattern here for debugging,
                            // it should be a 32-bit counter that increments by one every 4 microseconds
                            TCPState tstate(0xAAAA5555, LISTEN, NUM_RETRIES); // TODO 1000?
                            auto con = ConnectionToStateMapping<TCPState>(s.connection, Time(), tstate, false); // TODO false?
                            clist.push_back(con);
                        } else {
                            cerr << "[ERROR] ACCEPT: Following connection is already in use:\n" << s.connection << "\n";
                            continue;
                        }

                        resp.type = STATUS;
                        // resp.connection = s.connection;
                        // resp.bytes = 0;
                        resp.error = EOK;
                        MinetSend(sock, resp);
                        MinetSendToMonitor(MinetMonitoringEvent("LISTEN CREATED"));
                        cerr << "Listening on connection: \n" << s.connection << "\n";
                        break;
                    }
                    case WRITE: {

                        break;
                    }
                    case FORWARD: {

                        break;
                    }
                    case CLOSE: {
                        auto cs = clist.FindMatching(s.connection);
                        if (cs != clist.end()) {
                            //
                            if (cs->state.GetState() == CLOSE_WAIT) {
                                // send FIN and go to LAST_ACK
                                Packet p;
                                Buffer data(NULL, 0);
                                unsigned char flags = 0;
                                SET_FIN(flags);
                                SET_ACK(flags);
                                makePacket(p, data, s.connection, flags,
                                           WIN_SIZE, cs->state.GetLastSent(),
                                           cs->state.GetLastAcked());
                                int sent_status = MinetSend(mux, p);
                                if (sent_status == 0) {
                                    cs->state.SetState(LAST_ACK);
                                } else {
                                    cerr << "[ERROR] FIN ACK not sent\n";
                                }
                            } else if (cs->state.GetState() == SYN_RCVD
                                       || cs->state.GetState() == ESTABLISHED) {

                            } else if (cs->state.GetState() == SYN_SENT) {

                            }

                        } else {
                            cerr << "[ERROR] CLOSE: noting to close";
                            resp.type = STATUS;
                            // should be a error code
                            resp.error = ENOMATCH;
                            MinetSend(sock, resp);
                        }
                        break;
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
               unsigned int seq_num, unsigned int ack_num)
{

    // TODO
    unsigned int num_bytes = data.GetSize();
    p = Packet(data.ExtractFront(num_bytes));
    cout << c << "\n";

    // header
    // IP header

    IPHeader iph;
    iph.SetDestIP(c.dest);
    iph.SetSourceIP(c.src);
    iph.SetProtocol(c.protocol);
    iph.SetTotalLength(IP_HEADER_BASE_LENGTH + TCP_HEADER_BASE_LENGTH + num_bytes);
    iph.SetID((unsigned short)rand());
    p.PushFrontHeader(iph);

    // TCPHeader
    TCPHeader tcph;
    tcph.SetSourcePort(c.srcport, p);
    tcph.SetDestPort(c.destport, p);
    tcph.SetSeqNum(seq_num, p);
    tcph.SetAckNum(ack_num, p);
    tcph.SetHeaderLen(TCP_HEADER_BASE_LENGTH / 4, p);
    tcph.SetWinSize(win_size, p);
    tcph.SetFlags(flags, p);
    p.PushBackHeader(tcph);

//    cerr << iph;
//    cerr << tcph;

    return 0;
}
