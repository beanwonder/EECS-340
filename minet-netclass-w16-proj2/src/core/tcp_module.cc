#include <sys/time.h> #include <sys/types.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <iostream>
#include <algorithm>

#include "Minet.h"
#include "tcpstate.h"


using std::cout;
using std::endl;
using std::cerr;
using std::string;

#define NUM_RETRIES 3
#define WIN_SIZE    14600 // limited by the size of N in TCPState
#define TIMEOUT_THD 2     // 2 second timeout threshold
#define TIMEOUT_CLK 0.1   // 0.1 second timeout clock

int makePacket(Packet& p, Buffer& data, Connection& c, unsigned char flags,
               unsigned int winSize, unsigned int segNum, unsigned int ackNum = 0);

int go_back_N_send_data(ConnectionList<TCPState>::iterator &cs, unsigned int start, unsigned int len, unsigned int start_seq, MinetHandle &mux);

void printPacket(Packet& p);
void deadloop();
void handleTimeout(ConnectionList<TCPState>::iterator &cs, MinetHandle &mux, MinetHandle &sock);
void handleExpireTimerTries(ConnectionList<TCPState>::iterator &cs, MinetHandle &mux, MinetHandle &sock);

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

    while (MinetGetNextEvent(event, TIMEOUT_CLK)==0) {
        if (event.eventtype == MinetEvent::Timeout) {
            // cout << "[Timeout]: \n";
            Time currentTime;
            // seems not work
            // for (ConnectionToStateMapping<TCPState> cs : clist) {
               for (ConnectionList<TCPState>::iterator cs = clist.begin(); cs != clist.end(); ++cs) {
                // find a timeoutted connection
                if (cs->bTmrActive && (cs->timeout < currentTime)) {
                    cout << "[Timeout] find a timeout event\n";

                    if(cs->state.ExpireTimerTries()) {
                        if (cs->state.GetState() == CLOSED) {
                            cout << "[Timeout] Connnection " << cs->connection << " is deleted!!!\n";
                            clist.erase(cs);
                        } else {
                            cout << "[Timeout] No more tries...\n";
                            handleExpireTimerTries(cs, mux, sock);
                        }
                     } else {
                        if (cs->state.GetState() == CLOSED) {
                            cout << "[Timeout] Connnection " << cs->connection << " is deleted!!!\n";
                            clist.erase(cs);
                        } else {
                            handleTimeout(cs, mux, sock);
                        }
                    }
                }
            }
        }
        // if we received an unexpected type of event, print error
        else if (event.eventtype!=MinetEvent::Dataflow || event.direction!=MinetEvent::IN) {
          MinetSendToMonitor(MinetMonitoringEvent("Unknown event ignored."));
          cerr << "Invalid event from Minet\n" << endl;
        } else {
          //  Data from the IP layer below  //

            if (event.handle==mux) {
                cout << "\nEvent from mux received\n";
                Packet p;
                MinetReceive(mux,p);
                unsigned tcphlen=TCPHeader::EstimateTCPHeaderLength(p);
                //cerr << "Estimated TCP header len=" << tcphlen << "\n";
                p.ExtractHeaderFromPayload<TCPHeader>(tcphlen);
                IPHeader iph = p.FindHeader(Headers::IPHeader);
                TCPHeader tcph = p.FindHeader(Headers::TCPHeader);
                Connection c;
                printPacket(p);

                if (!tcph.IsCorrectChecksum(p)) {
                    cerr << "[ERROR] Checksum check FAILED!!!\n";
                    // if the packet is corrupted, discard it.
                    continue;
                }

                // Identifty the connection with 5-tuple
                iph.GetDestIP(c.src);
                iph.GetSourceIP(c.dest);
                iph.GetProtocol(c.protocol);
                tcph.GetDestPort(c.srcport);
                tcph.GetSourcePort(c.destport);

                auto cs = clist.FindMatching(c);

                if(cs == clist.end()) {
                    cerr << "[ERROR] Invalid connection detected!!!\n";
                    continue;
                }

                SockRequestResponse resp;
                cerr << tcph << "\n";
                cerr << c << "\n";

                switch (cs->state.GetState())
                {
                    case LISTEN:
                    {
                        cout << "Listen: Passive open ...\n";
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
                                printPacket(p);
                                auto nts = TCPState(seq_num, SYN_RCVD, 10);
                                nts.last_acked = seq_num - 1;
                                nts.last_recvd = ack_num - 1;
                                auto nc  = ConnectionToStateMapping<TCPState> (c, Time(), nts, false);
                                clist.push_front(nc);
                                MinetSendToMonitor(MinetMonitoringEvent("SYN ACK SENT"));

                                SockRequestResponse repl;
                                repl.type = WRITE;
                                repl.bytes = 0;
                                // repl.data = ?
                                MinetSend(sock, repl);

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
                            SockRequestResponse repl(WRITE, cs->connection, Buffer(), 0, EOK);
                            int send_status = MinetSend(sock, repl);
                            if (send_status == 0) {
                            cout << "state SYN_RCVD -> ESTABLISHED \n";
                                cs->state.SetState(ESTABLISHED);
                            }
                        } else {
                            cerr << "SYN_RCVD: discard all other packet\n";
                        }
                        break;
                    }

                    case ESTABLISHED:
                    {
                        cout << "ESTABLISHED: \n";
                        unsigned char flags;
                        tcph.GetFlags(flags);

                        unsigned int recvd_ack;
                        unsigned int recvd_seq;
                        tcph.GetAckNum(recvd_ack);
                        tcph.GetSeqNum(recvd_seq);

                        // Dealing with acks
                        if (IS_ACK(flags)) {
                            // data acks and erase from buffer
                            if (recvd_ack > cs->state.GetLastAcked() + 1 &&
                                recvd_ack <= cs->state.GetLastSent() + 1) {
                                cs->bTmrActive = false;
                                cout << "Timer Stopped\n";
                                cs->state.SendBuffer.Erase(0, recvd_ack - cs->state.GetLastAcked() - 1);
                                cs->state.SetLastAcked(recvd_ack - 1);

                                // reset timeout
                                Time currentTime;
                                cs->timeout = currentTime + Time(TIMEOUT_THD);
                                cs->state.tmrTries = 1;
                            } else {
                                cout << "[ERROR] invalid ack number:\n" << recvd_ack << "\n";
                                cout << "        lastacked: " << cs->state.GetLastAcked() << "\n";
                                cout << "        lastSent : " << cs->state.GetLastSent() << "\n";
                            }
                        }

                        if (IS_FIN(flags)) {
                            unsigned int seq;
                            tcph.GetSeqNum(seq);

                            if (seq == cs->state.GetLastRecvd() + 1) {
                                Packet p;
                                Buffer data(NULL, 0);
                                unsigned char flags = 0; // the shadowning should not be a problem here
                                SET_ACK(flags);
                                makePacket(p, data, c, flags, WIN_SIZE, cs->state.GetLastSent() + 1, seq + 1); // TODO window size?
                                // TODO send EOF to sock
                                MinetSend(mux, p);
                                printPacket(p);
                                cs->state.SetLastRecvd(seq);
                                cs->state.SetLastSent(cs->state.GetLastSent() + 1);
                                cs->state.SetState(CLOSE_WAIT);
                                cout << "ACK packet sent\n";

                                Time currentTime;
                                cs->timeout = currentTime + Time(TIMEOUT_THD);
                                cs->bTmrActive = true;
                                cout << "time out set\n";

                                // notify the sock that the connection is terminated safely
                                SockRequestResponse repl(STATUS, cs->connection, Buffer(), 0, EOK);
                                MinetSend(sock, resp);
                            }
                        }

                        // Dealing with data
                        else if (recvd_seq == cs->state.GetLastRecvd() + 1) {
                            unsigned short len; // extracted data len
                            unsigned char iphlen;

                            iph.GetHeaderLength(iphlen);
                            iph.GetTotalLength(len);
                            len -= (iphlen * 4 + tcphlen);
                            Buffer &recvd_data = p.GetPayload().ExtractFront(len);

                            cout << "recvd_data\n";
                            cout << recvd_data << "\n";

                            //
                            if (recvd_data.GetSize() > 0) {
                                if (cs->state.RecvBuffer.GetSize() + recvd_data.GetSize() <= cs->state.TCP_BUFFER_SIZE) {

                                    //cs->state.RecvBuffer.AddBack(recvd_data);
                                    cs->state.SetLastRecvd(cs->state.GetLastRecvd() + recvd_data.GetSize());

                                    // send ack back;
                                    Packet p;
                                    unsigned char f = 0;
                                    SET_ACK(f);
                                    Buffer b(NULL, 0);
                                    makePacket(p, b, c, f, WIN_SIZE,
                                               cs->state.GetLastSent() + 1, cs->state.GetLastRecvd() + 1);
                                    cs->state.SetLastSent(cs->state.GetLastSent() + 1);

                                    int sent_status = MinetSend(mux, p);
                                    if (sent_status == 0) {
                                        cout << "ACK sent\n";
                                        printPacket(p);
                                    }

                                    // send to sock
                                    if (sent_status == 0) {
                                        SockRequestResponse repl(WRITE, cs->connection, recvd_data, recvd_data.GetSize(), EOK);
                                        MinetSend(sock, repl);
                                    }
                                }
                            } else {
                                cout << "No data payload\n";
                                cs->state.SetLastRecvd(cs->state.GetLastRecvd() + 1);
                            }
                        }


                        if (IS_SYN(flags) || IS_PSH(flags) || IS_URG(flags)) {
                            cerr << "[ERROR] Invalid flags detected!\n";
                        }

                        break;
                    }

                    case SYN_SENT:
                    {
                        cout << "SYN_SENT: Active open ack...\n";
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
                            if (recAckNum <= ts.last_acked + 1) {
                                cerr << "[ERROR] invalid ack number received: " << recAckNum << "\n";
                                continue;
                            }

                            // Update TCP State
                            ts.last_recvd = recSeqNum; // + data size
                            ts.last_acked = recAckNum - 1;

                            makePacket(p, data, c, flags, WIN_SIZE, ts.last_sent, recSeqNum + 1);
                            MinetSend(mux, p);
                            cout << "SYN ACK pakcet sent\n";

                            // Update connection state
                            cs->state.SetState(ESTABLISHED);
                            cs->bTmrActive = false;

                            // Sent respond to sock
                            SockRequestResponse resp(WRITE, cs->connection, Buffer(), 0, EOK);
                            MinetSend(sock, resp);
                        } else {
                            cerr << "[ERROR] Invalud flags: " << flags << "\n";
                            continue;
                        }
                        break;
                    }

                    case CLOSE_WAIT:
                    {
                        cout << "CLOSE WAIT:\n";
                        cout << "Should not be here\n";

                        break;
                    }

                    case LAST_ACK:
                    {
                        cout << "LAST ACK: \n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if (IS_ACK(flags)) {
                            unsigned int seq;
                            tcph.GetSeqNum(seq);
                            if (seq == cs->state.GetLastRecvd() + 1) {
                                unsigned int ack;
                                tcph.GetAckNum(ack);
                                if (ack > cs->state.GetLastSent()) {
                                    cs->state.SetLastAcked(ack - 1);
                                    cs->state.SetState(CLOSE);
                                    clist.erase(cs);
                                } else {
                                    cerr << "[ERROR] ACK mismatch detected!\n";
                                }
                            } else {
                                cout << "[ERROR] Discarding unordered pakcet...\n";
                            }
                        }
                        if (!IS_ACK(flags) || IS_SYN(flags) || IS_FIN(flags) || IS_PSH(flags) || IS_URG(flags)) {
                            cerr << "[ERROR] Invalid flags detected\n";
                        }
                        break;
                    }

                    case FIN_WAIT1:
                    {
                        cout << "FIN_WAIT_1...\n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if (IS_ACK(flags)) {
                            unsigned int seq;
                            tcph.GetSeqNum(seq);
                            if (seq == cs->state.GetLastRecvd() + 1) {
                                unsigned int ack;
                                tcph.GetAckNum(ack);
                                if (ack > cs->state.GetLastSent()) {
                                    cs->state.SetLastAcked(ack - 1);
                                    cs->state.SetState(FIN_WAIT2);
                                } else {
                                    cerr << "[ERROR] ACK mismatch detected!\n";
                                }
                            } else {
                                cout << "[ERROR] Discarding unordered pakcet...\n";
                            }
                        }
                        if (IS_FIN(flags)) {
                            unsigned int seq;
                            tcph.GetSeqNum(seq);
                            if (seq == cs->state.GetLastRecvd() + 1) {
                                Packet p;
                                Buffer data(NULL, 0);
                                unsigned char flags = 0; // the shadowning should not be a problem here
                                SET_ACK(flags);
                                makePacket(p, data, c, flags, WIN_SIZE, cs->state.GetLastSent() + 1, seq + 1); // TODO window size?
                                MinetSend(mux, p);
                                cs->state.SetLastRecvd(seq);
                                cs->state.SetLastSent(cs->state.GetLastSent() + 1);
                                cs->state.SetState(TIME_WAIT);

                                // notify the sock that the connection is terminated safely
                                resp.type       = STATUS;
                                resp.connection = c;
                                resp.error      = EOK;
                                MinetSend(sock, resp);
                            }
                        }
                        if (!IS_ACK(flags) || IS_SYN(flags) || IS_PSH(flags) || IS_URG(flags)) {
                            cerr << "[ERROR] Invalid flags detected!\n";
                        }
                        break;
                    }

                    case FIN_WAIT2:
                    {
                        cout << "FIN_WAIT_2...\n";
                        unsigned char flags;
                        tcph.GetFlags(flags);
                        if (IS_ACK(flags)) {
                            unsigned int ack;
                            tcph.GetAckNum(ack);
                            if (ack > cs->state.GetLastAcked()) { // TODO waht about wrap around?
                                cs->state.SetLastAcked(ack - 1);
                            }
                        }
                        if (IS_FIN(flags)) {
                            unsigned int seq;
                            tcph.GetSeqNum(seq);
                            if (seq == cs->state.GetLastRecvd() + 1) {
                                Packet p;
                                Buffer data(NULL, 0);
                                unsigned char flags = 0; // the shadowning should not be a problem here
                                SET_ACK(flags);
                                makePacket(p, data, c, flags, WIN_SIZE, cs->state.GetLastSent() + 1, seq + 1); // TODO window size?
                                MinetSend(mux, p);
                                cs->state.SetLastRecvd(seq);
                                cs->state.SetLastSent(cs->state.GetLastSent() + 1);
                                cs->state.SetState(TIME_WAIT);

                                // notify the sock that the connection is terminated safely
                                resp.type       = STATUS;
                                resp.connection = c;
                                resp.error      = EOK;
                                MinetSend(sock, resp);
                            } else {
                                cerr << "[ERROR] Discarding unordered packets!\n";
                            }
                        }
                        if (!IS_FIN(flags) ||  IS_SYN(flags) || IS_URG(flags) || IS_PSH(flags)) {
                            cerr << "[ERROR] Invaid flags detected\n";
                        }
                        break;
                    }

                    case TIME_WAIT:
                    {
                        cout << "Time Wait...\n";
                        cout << "Discarding this packet...\n";
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
                            printPacket(p);
                            if (send_status == 0) {
                                cout << "SYN packet sent\n";
                                TCPState ts(seqNum, SYN_SENT, NUM_RETRIES);
                                ts.last_acked = seqNum - 1;
                                cout << "GetLastAcked: " << ts.GetLastAcked() << "\n";
                                ts.tmrTries = NUM_RETRIES;
                                auto con = ConnectionToStateMapping<TCPState>(s.connection, Time() + Time(TIMEOUT_THD), ts, true);
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
                    case WRITE:
                    {
                        auto cs = clist.FindMatching(s.connection);
                        if (cs != clist.end() && cs->state.GetState() == ESTABLISHED) {
                            // lets Go back N
                            // add data to sender buffer
                            if (cs->state.SendBuffer.GetSize() + s.data.GetSize() <= cs->state.TCP_BUFFER_SIZE) {

                                cs->state.SendBuffer.AddBack(s.data);
                                unsigned int send_not_acked = cs->state.GetLastSent() - cs->state.GetLastAcked();
                                unsigned int left_send_win_size = cs->state.GetN() - send_not_acked;

                                if (left_send_win_size > 0) {
                                    unsigned int data_not_send = cs->state.SendBuffer.GetSize() - send_not_acked;
                                    // send size
                                    unsigned int data_to_send = std::min(left_send_win_size, data_not_send);
                                    // data_to_send = std::min(TCP_MAXIMUM_SEGMENT_SIZE, data_to_send); // exclude header
                                    //
                                    int data_sent = go_back_N_send_data(cs, send_not_acked, data_to_send, cs->state.GetLastSent()+1, mux);

                                    if (data_sent > 0) {
                                        cs->state.SetLastSent(cs->state.GetLastSent() + data_sent);
                                        SockRequestResponse repl(STATUS, s.connection, Buffer(), data_sent, EOK);
                                        MinetSend(sock, repl);
                                        cs->bTmrActive = true;

                                        // open timer for go backN
                                        cs->bTmrActive = true;
                                        Time currentTime;
                                        cs->timeout = currentTime + Time(TIMEOUT_THD);
                                        cs->state.tmrTries = 1;

                                    }
                                    // if data to send size > segment
                                    /*
                                    int offset = send_not_acked;
                                    // send packet
                                    Packet p;
                                    unsigned char flags = 0;
                                    SET_ACK(flags);
                                    char buf[TCP_MAXIMUM_SEGMENT_SIZE] = {0};
                                    cs->state.SendBuffer.GetData(buf, data_to_send, offset);
                                    Buffer d(buf, data_to_send);

                                    makePacket(p, d, s.connection, flags,
                                               cs->state.GetN(), cs->state.GetLastSent()+1, cs->state.GetLastAcked());
                                    int send_status = MinetSend(mux, p);

                                    if (send_status == 0) {
                                        cs->state.SetLastSent(cs->state.GetLastSent() + data_to_send);
                                        SockRequestResponse repl(STATUS, s.connection, Buffer(), data_to_send, EOK);
                                        MinetSend(sock, repl);

                                        // open timer for go backN
                                        cs->bTmrActive = true;
                                        Time currentTime;
                                        cs->timeout = currentTime + Time(TIMEOUT_THD);
                                        cs->state.tmrTries = 1;
                                    }
                                    */
                                } else {
                                    if (left_send_win_size < 0) {
                                        cerr << "ERROR NEVER REACH HERE\n";
                                    }
                                }
                            } else {
                                cout << "RecvBuffer Overflow\n";
                                SockRequestResponse repl(STATUS, s.connection, Buffer(), 0, EBUF_SPACE);
                                MinetSend(sock, repl);
                            }

                            /*
                            // not go back N
                            if (cs->state.GetLastSent() == cs->state.GetLastAcked()) {
                                if (cs->state.SendBuffer.GetSize() != 0) {
                                    cout << "[ERROR] Send buffer error\n";
                                    deadloop();
                                }
                                if (cs->state.SendBuffer.GetSize() + s.data.GetSize() <=
                                    cs->state.TCP_BUFFER_SIZE) {
                                    cs->state.SendBuffer.AddBack(s.data);
                                } else {
                                    // TODO split the large packet later
                                    cout << "[ERROR] Send packet too large!\n";
                                    deadloop();
                                }

                                Packet p;
                                unsigned char flags = 0;
                                //SET_ACK(flags);
                                makePacket(p, cs->state.SendBuffer, s.connection, flags,
                                           WIN_SIZE, cs->state.GetLastSent() + 1, cs->state.GetLastAcked() + 1);
                                int send_status = MinetSend(mux, p);
                                Time currentTime;
                                cs->timeout = currentTime + Time(TIMEOUT_THD);
                                cs->bTmrActive = true;
                                cout << "Timer Started\n";
                                printPacket(p);
                                if (send_status == 0) {
                                    cs->state.SetLastSent(cs->state.GetLastSent() + s.data.GetSize());
                                    //SockRequestResponse resp(STATUS, s.connection, Buffer(), s.data.GetSize(), EOK);
                                    SockRequestResponse resp(STATUS, s.connection, Buffer(), 0, EOK);
                                    MinetSend(sock, resp);
                                } else {
                                    cout << "[ERROR] Send Data Packet Failed!\n";
                                    SockRequestResponse resp(STATUS, s.connection, Buffer(), 0, EBUF_SPACE);
                                    MinetSend(sock, resp);
                                }
                            } else {
                                cout << "[ERROR]\n";
                            }
                            */
                        } else {
                                cout << "[ERROR] WRITE: Unmatched connection\n";
                                SockRequestResponse resp(STATUS, s.connection, Buffer(), 0, ENOMATCH);
                                MinetSend(sock, resp);
                        }
                        break;
                    }
                    case FORWARD: {
                        // ignore do nothing
                        break;
                    }
                    case CLOSE: {
                        auto cs = clist.FindMatching(s.connection);
                        if (cs != clist.end()) {
                            //
                            if (cs->state.GetState() == CLOSE_WAIT) {
                                cout << "Passive close...\n";
                                // send FIN and go to LAST_ACK
                                Packet p;
                                Buffer data(NULL, 0);
                                unsigned char flags = 0;
                                SET_FIN(flags);
                                unsigned int seqNum = cs->state.GetLastSent() + 1;
                                makePacket(p, data, s.connection, flags,
                                           cs->state.GetN(), seqNum,
                                           cs->state.GetLastAcked());
                                int sent_status = MinetSend(mux, p);
                                if (sent_status == 0) {
                                    cs->state.SetLastSent(seqNum);
                                    cs->state.SetState(LAST_ACK);
                                    // reset timer
                                    cs->state.tmrTries = 3;
                                    Time currentTime;
                                    cs->timeout = currentTime + Time(TIMEOUT_THD);
                                    cs->bTmrActive = true;
                                } else {
                                    cerr << "[ERROR] FIN not sent\n";
                                    continue;
                                }
                            } else if (cs->state.GetState() == SYN_RCVD
                                       || cs->state.GetState() == ESTABLISHED) {
                                cout << "Active close...\n";
                                // send FIN and go to FIN_WAIT_1
                                Packet p;
                                Buffer data(NULL, 0);
                                unsigned char flags = 0;
                                SET_FIN(flags);
                                unsigned int seqNum = cs->state.GetLastSent() + 1;
                                makePacket(p, data, s.connection, flags,
                                           cs->state.GetN(), seqNum,
                                           cs->state.GetLastAcked());
                                int sent_status = MinetSend(mux, p);
                                if (sent_status == 0) {
                                    cs->state.SetLastSent(seqNum);
                                    cs->state.SetState(FIN_WAIT1);
                                    // reset timer
                                    cs->state.tmrTries = 3;
                                    Time currentTime;
                                    cs->timeout = currentTime + Time(TIMEOUT_THD);
                                    cs->bTmrActive = true;
                                } else {
                                    cerr << "[ERROR] FIN not sent\n";
                                    continue;
                                }

                            } else if (cs->state.GetState() == SYN_SENT) {
                                cout << "SYN_SNET -> CLOSE by appl\n";
                                clist.erase(cs);
                            }
                            resp.type = STATUS;
                            resp.error = EOK;
                            MinetSend(sock, resp);
                            cout << "OK response sent to sock\n";

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

void printPacket(Packet& p)
{
    cout << p << "\n";
    TCPHeader th = (TCPHeader)p.FindHeader(Headers::TCPHeader);
    IPHeader ih  = (IPHeader) p.FindHeader(Headers::IPHeader);
    cout << th << "\n";
    cout << ih << "\n";
    return;
}

void deadloop()
{
    cout << "ENTERED DEAD LOOP !!!!!!\n";
    cout << "ENTERED DEAD LOOP !!!!!!\n";
    cout << "ENTERED DEAD LOOP !!!!!!\n";
    while(1) {};
}

void handleExpireTimerTries(ConnectionList<TCPState>::iterator &cs, MinetHandle &mux, MinetHandle &sock)
{

    switch (cs->state.GetState()) {
        case TIME_WAIT:
        case LAST_ACK:
        case SYN_SENT:
        {
            // treistimes expired close
            cs->state.SetState(CLOSED);
            Time currentTime;
            cs->timeout = currentTime + Time(TIMEOUT_THD);
            cout << "TIME_WAIT LAST_ACK SYN_SENT -> CLOSE\n";
            cs->state.tmrTries= 0;
            break;
        }

        case SYN_RCVD:
        {
            // TODO timout, send RST
            Packet p;
            Buffer d(NULL, 0);
            unsigned char f = 0;
            SET_RST(f);
            unsigned int seq_num = cs->state.GetLastSent() + 1;
            // unsigned int ack_num = ;

            makePacket(p, d, cs->connection, f, WIN_SIZE, seq_num, cs->state.GetLastSent());
            int sent_status = MinetSend(mux, p);
            if (sent_status == 0) {
                cout << "SYN_RCVD -> CLOSED\n";
                cs->state.SetState(CLOSED);
                Time current_time;
                cs->timeout = current_time + Time(TIMEOUT_THD);
            }
            break;
        }

    }
    return;
}

void handleTimeout(ConnectionList<TCPState>::iterator &cs, MinetHandle &mux, MinetHandle &sock)
{
    switch (cs->state.GetState()) {
        case SYN_RCVD:
        {
            cout << "[Timeout] SYN_RCVD\n";
            // resend SYN ACK packet
            Packet p;
            Buffer d(NULL, 0);
            unsigned char flags = 0;
            SET_SYN(flags);
            SET_ACK(flags);
            unsigned int seqNum = cs->state.GetLastAcked() + 1;
            makePacket(p, d, cs->connection, flags, WIN_SIZE, seqNum, cs->state.GetLastRecvd() + 1);
            int sent_status = MinetSend(mux, p);
            if (sent_status == 0) {
                Time currentTime;
                cs->timeout = currentTime + Time(TIMEOUT_THD);
            }
            break;
        }

        case SYN_SENT:
        {
            cout << "[Timeout] SYN_SENT\n";
            // resend SYN packet
            Packet p;
            Buffer d(NULL, 0);
            unsigned char flags = 0;
            SET_SYN(flags);
            unsigned int seqNum = cs->state.GetLastAcked() + 1;
            cout << "[Timeout] GetLastAcked: " << cs->state.GetLastAcked() << "\n";
            makePacket(p, d, cs->connection, flags, WIN_SIZE, seqNum, 0);
            printPacket(p);
            int sent_status = MinetSend(mux, p);
            if (sent_status == 0) {
                Time currentTime;
                cs->timeout = currentTime + Time(TIMEOUT_THD);
            }
            break;
        }

        case CLOSE_WAIT: {
            cout <<  "[Timeout] CLOSE_WAIT\n";
            // resend the finish packet
            Packet p;
            Buffer d(NULL, 0);
            unsigned char flags = 0;
            SET_FIN(flags);
            unsigned int seq_num = cs->state.GetLastAcked() + 1;
            makePacket(p, d, cs->connection, flags, WIN_SIZE, seq_num, cs->state.GetLastRecvd() + 1);
            int sent_status = MinetSend(mux, p);
            if (sent_status == 0) {
                cout << "CLOS_WAIT -> LAST_ACK\n";
                // TODO set set last sent ?
                cs->state.SetState(LAST_ACK);
                //cs->bTmrActive = false;
                Time currentTime;
                cs->timeout = currentTime + Time(TIMEOUT_THD);
                cs->state.tmrTries = 1;
            }
            break;
        }

        case LAST_ACK:
        case FIN_WAIT1:
        {
            cout << "[Timeout] LACK_ACK / FIN_WAIT_1\n";
            // resend the finish packet
            Packet p;
            Buffer d(NULL, 0);
            unsigned char flags = 0;
            SET_FIN(flags);
            unsigned int seqNum = cs->state.GetLastAcked() + 1;
            makePacket(p, d, cs->connection, flags, WIN_SIZE, seqNum, cs->state.GetLastRecvd() + 1);
            int sent_status = MinetSend(mux, p);
            if (sent_status == 0) {
                Time currentTime;
                cs->timeout = currentTime + Time(TIMEOUT_THD);
            }
            break;
        }

        case TIME_WAIT:
        {
            // Wait 120s (TIMEOUT_THD * 60 expires);
            Time current_time;
            cs->timeout = current_time + Time(TIMEOUT_THD);
            cs->state.tmrTries = 60;
            break;
        }
        case ESTABLISHED:
        {
            unsigned int data_resend = go_back_N_send_data(cs, 0, cs->state.GetLastSent() - cs->state.GetLastAcked(),
                                                           cs->state.GetLastSent()+1, mux);
            if (data_resend > 0) {
                Time currentTime;
                cs->timeout = currentTime + Time(TIMEOUT_THD);
            }

            break;
        }
    }
    return;
}

int go_back_N_send_data(ConnectionList<TCPState>::iterator &cs, unsigned int start, unsigned int len, unsigned int start_seq, MinetHandle &mux)
{
    unsigned int data_to_send = len;
    unsigned int offset = start;
    unsigned int seq_num = start_seq;
    Buffer &send_buffer = cs->state.SendBuffer;

    while (data_to_send > TCP_MAXIMUM_SEGMENT_SIZE) {
        Packet p;
        unsigned char flags = 0;
        SET_ACK(flags);
        char buf[TCP_MAXIMUM_SEGMENT_SIZE] = {0};
        send_buffer.GetData(buf, TCP_MAXIMUM_SEGMENT_SIZE, offset);
        Buffer d(buf, TCP_MAXIMUM_SEGMENT_SIZE);

        makePacket(p, d, cs->connection, flags,
                   cs->state.GetN(), seq_num,
                   cs->state.GetLastAcked());
        int send_status = MinetSend(mux, p);
        if (send_status == 0) {
            data_to_send -= TCP_MAXIMUM_SEGMENT_SIZE;
            offset       += TCP_MAXIMUM_SEGMENT_SIZE;
            seq_num      += TCP_MAXIMUM_SEGMENT_SIZE;
        } else {
            return offset;
        }
    }

    if (data_to_send > 0) {
        // send last data
        Packet p;
        unsigned char flags = 0;
        SET_ACK(flags);
        char buf[TCP_MAXIMUM_SEGMENT_SIZE] = {0};
        send_buffer.GetData(buf, data_to_send, offset);
        Buffer d(buf, data_to_send);

        makePacket(p, d, cs->connection, flags,
                   cs->state.GetN(), seq_num,
                   cs->state.GetLastAcked());
        int send_status = MinetSend(mux, p);
        if (send_status == 0) {
            offset       += data_to_send;
            data_to_send -= data_to_send;
            offset       += data_to_send;
            seq_num      += data_to_send;
        }
    }
    return offset;
}
