
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h> 

// =====================================

#define RTO 500000 /* timeout in microseconds */
#define HDR_SIZE 12 /* header size*/
#define PKT_SIZE 524 /* total packet size */
#define PAYLOAD_SIZE 512 /* PKT_SIZE - HDR_SIZE */
#define WND_SIZE 10 /* window size*/
#define MAX_SEQN 25601 /* number of sequence numbers [0-25600] */
#define FIN_WAIT 2 /* seconds to wait after receiving FIN*/

// Packet Structure: Described in Section 2.1.1 of the spec. DO NOT CHANGE!
struct packet {
    unsigned short seqnum;
    unsigned short acknum;
    char syn;
    char fin;
    char ack;
    char dupack;
    unsigned int length;
    char payload[PAYLOAD_SIZE];
};

// Printing Functions: Call them on receiving/sending/packet timeout according
// Section 2.6 of the spec. The content is already conformant with the spec,
// no need to change. Only call them at correct times.
void printRecv(struct packet* pkt) {
    printf("RECV %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", (pkt->ack || pkt->dupack) ? " ACK": "");
}

void printSend(struct packet* pkt, int resend) {
    if (resend)
        printf("RESEND %d %d%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "");
    else
        printf("SEND %d %d%s%s%s%s\n", pkt->seqnum, pkt->acknum, pkt->syn ? " SYN": "", pkt->fin ? " FIN": "", pkt->ack ? " ACK": "", pkt->dupack ? " DUP-ACK": "");
}

void printTimeout(struct packet* pkt) {
    printf("TIMEOUT %d\n", pkt->seqnum);
}

// Building a packet by filling the header and contents.
// This function is provided to you and you can use it directly
void buildPkt(struct packet* pkt, unsigned short seqnum, unsigned short acknum, char syn, char fin, char ack, char dupack, unsigned int length, const char* payload) {
    pkt->seqnum = seqnum;
    pkt->acknum = acknum;
    pkt->syn = syn;
    pkt->fin = fin;
    pkt->ack = ack;
    pkt->dupack = dupack;
    pkt->length = length;
    memcpy(pkt->payload, payload, length);
}

// =====================================

double setTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) RTO/1000000;
}

double setFinTimer() {
    struct timeval e;
    gettimeofday(&e, NULL);
    return (double) e.tv_sec + (double) e.tv_usec/1000000 + (double) FIN_WAIT;
}

int isTimeout(double end) {
    struct timeval s;
    gettimeofday(&s, NULL);
    double start = (double) s.tv_sec + (double) s.tv_usec/1000000;
    return ((end - start) < 0.0);
}

// =====================================

int main (int argc, char *argv[])
{
    if (argc != 5) {
        perror("ERROR: incorrect number of arguments\n "
               "Please use \"./client <HOSTNAME-OR-IP> <PORT> <ISN> <FILENAME>\"\n");
        exit(1);
    }

    struct in_addr servIP;
    if (inet_aton(argv[1], &servIP) == 0) {
        struct hostent* host_entry; 
        host_entry = gethostbyname(argv[1]); 
        if (host_entry == NULL) {
            perror("ERROR: IP address not in standard dot notation\n");
            exit(1);
        }
        servIP = *((struct in_addr*) host_entry->h_addr_list[0]);
    }

    unsigned int servPort = atoi(argv[2]);
    unsigned short initialSeqNum = atoi(argv[3]);

    FILE* fp = fopen(argv[4], "r");
    if (fp == NULL) {
        perror("ERROR: File not found\n");
        exit(1);
    }

    // =====================================
    // Socket Setup

    int sockfd;
    struct sockaddr_in servaddr;
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr = servIP;
    servaddr.sin_port = htons(servPort);
    memset(servaddr.sin_zero, '\0', sizeof(servaddr.sin_zero));

    int servaddrlen = sizeof(servaddr);

    // NOTE: We set the socket as non-blocking so that we can poll it until
    //       timeout instead of getting stuck. This way is not particularly
    //       efficient in real programs but considered acceptable in this
    //       project.
    //       Optionally, you could also consider adding a timeout to the socket
    //       using setsockopt with SO_RCVTIMEO instead.
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // =====================================
    // Establish Connection: This procedure is provided to you directly and is
    // already working.
    // Note: The third step (ACK) in three way handshake is sent along with the
    // first piece of along file data thus is further below

    struct packet synpkt, synackpkt;
    unsigned short seqNum = initialSeqNum;

    buildPkt(&synpkt, seqNum, 0, 1, 0, 0, 0, 0, NULL);

    printSend(&synpkt, 0);
    sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    double timer = setTimer();
    int n;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &synackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            else if (isTimeout(timer)) {
                printTimeout(&synpkt);
                printSend(&synpkt, 1);
                sendto(sockfd, &synpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
        }

        printRecv(&synackpkt);
        if ((synackpkt.ack || synackpkt.dupack) && synackpkt.syn && synackpkt.acknum == (seqNum + 1) % MAX_SEQN) {
            seqNum = synackpkt.acknum;
            break;
        }
    }

    // =====================================
    // FILE READING VARIABLES
    
    char buf[PAYLOAD_SIZE];
    size_t m;

    // =====================================
    // CIRCULAR BUFFER VARIABLES

    struct packet ackpkt;
    struct packet pkts[WND_SIZE];
    int s = 0;
    int e = 0;
    int full = 0;

    // =====================================
    // Send First Packet (ACK containing payload)

    m = fread(buf, 1, PAYLOAD_SIZE, fp);

    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, m, buf);
    printSend(&pkts[0], 0);
    sendto(sockfd, &pkts[0], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    timer = setTimer();
    buildPkt(&pkts[0], seqNum, (synackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 0, 1, m, buf);

    e = 1;

    // =====================================
    // *** TODO: Implement the rest of reliable transfer in the client ***
    // Implement GBN for basic requirement or Selective Repeat to receive bonus

    // Note: the following code is not the complete logic. It only sends a
    //       single data packet, and then tears down the connection without
    //       handling data loss.
    //       Only for demo purpose. DO NOT USE IT in your final submission

    int prevRead = m;
    int seqNext = pkts[0].seqnum + PAYLOAD_SIZE;

    int currIndex;
    int start = s;
    int end = e;

    while (1) {
        n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
        
        if (n > 0) {
            printRecv(&ackpkt);

            if (seqNext == ackpkt.acknum) {
                s = (s + 1) % WND_SIZE;
                seqNext = (pkts[s].seqnum + m) % MAX_SEQN;
                start += 1;
            }
            else {
                for (int q = 0; q < WND_SIZE; q++) {
                    currIndex = pkts[(s + q) % WND_SIZE].seqnum + pkts[(s + q) % WND_SIZE].length;
                    if (currIndex % MAX_SEQN == ackpkt.acknum) {
                        start += q+1;
                        s = (s + q+1) % WND_SIZE;
                        break;
                    }
                }
            }
        }

        while(end - start < WND_SIZE){
            n = fread(buf, 1, PAYLOAD_SIZE, fp);

            if (n <= 0) break;
            else {
                seqNum = (seqNum + PAYLOAD_SIZE) % MAX_SEQN;
                m = n;
                buildPkt(&pkts[e], seqNum, 0, 0, 0, 0, 0, m, buf);
                printSend(&pkts[e], 0);
                sendto(sockfd, &pkts[e], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
                
                e = (e + 1) % WND_SIZE;
            }
            end++;

            if (n <= 0 && ackpkt.acknum == seqNum + m) break;
            // if(end - start < WND_SIZE) break;
        }

        if (n <= 0 && ackpkt.acknum == seqNum + m) break;

        if (isTimeout(timer)) {
            printTimeout(&pkts[s]);

            if(e == s) full = 1;
            else full = 0;

            if (full == 1) {
                for (int q = s; q < e + WND_SIZE; q++) {
                    printSend(&pkts[q % WND_SIZE], 1);
                    sendto(sockfd, &pkts[q % WND_SIZE], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                }
            }
            else {
                for (int q = s; q != e ; q = (q + 1) % WND_SIZE) {
                    printSend(&pkts[q], 1);
                    sendto(sockfd, &pkts[q], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                }
            }
            timer = setTimer();
        }

        if (n <= 0 && ackpkt.acknum == seqNum + m) break;
    }

    // while (1) {
        
    //     //checking for acks
    //     n = recvfrom(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);
    //     if(n > 0) {
    //         printRecv(&ackpkt);

    //         if(seqNext == ackpkt.acknum) {
    //             s = (s+1) % WND_SIZE;
    //             seqNext = (pkts[s].seqnum + prevRead) % MAX_SEQN;
                    
    //             if(e == s) full = 1;
    //             else full = 0;
    //         } else {
    //             //loop thru buffer and check acknum to seqnum?
    //             while(pkts[s].seqnum < ackpkt.acknum) { //possibly wrong... 
    //                 s = (s+1) % WND_SIZE;
                    
    //                 if(e == s) full = 1;
    //                 else full = 0;
    //             }
    //         }
            
    //         //printf("m: %d ; acknum: %d ; seqNum = %d ; prevRead: %d \n", m, ackpkt.acknum, seqNum, prevRead);      
    //     }

    //     if(n <= 0 && ackpkt.acknum == seqNum + prevRead) break;
        
    //     int len = 0;
    //     int start = s;
    //     int end = e;
    //     while (start != end) {
    //         start = (start + 1) % WND_SIZE;
    //         len++;
    //     }

    //     for(int i = 0; i < len; i++) {
    //         int currIndex = (s + i) % WND_SIZE;
    //         m = fread(buf, 1, PAYLOAD_SIZE, fp);
    //         if(m <= 0) break;
    //         else {
    //             prevRead = m;
    //             //update seqNum
    //             seqNum = (seqNum + prevRead) % MAX_SEQN;

    //             //build and send packet
    //             buildPkt(&pkts[currIndex], seqNum, 0, 0, 0, 0 , 0, prevRead, buf);                                                                                                                                                                                                                 
    //             printSend(&pkts[currIndex], 0);
    //             sendto(sockfd, &pkts[currIndex], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                
    //             //update e
    //             e = currIndex;

    //             //timer
    //             timer = setTimer();
    //         }

    //         if(e == s) full = 1;
    //         else full = 0;

    //         if(n <= 0 && ackpkt.acknum == seqNum + prevRead) break;
    //     }

    //     if(n <= 0 && ackpkt.acknum == seqNum + prevRead) break;

    //     //Handle timeout
    //     if (isTimeout(timer)) {
    //         printTimeout(&pkts[s]);
    //         int i = s;
    //         while(1) {
    //             printSend(&pkts[i], 1);
    //             sendto(sockfd, &pkts[i], PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    //             i = (i+1) % WND_SIZE;

    //             if(i == e) break;

    //             if(m <= 0 && ackpkt.acknum == seqNum + prevRead) {
    //                 break;
    //             }
    //         }
    //         timer = setTimer();
    //     }

    //     if(n <= 0 && ackpkt.acknum == seqNum + prevRead) break;
    // }

    // *** End of your client implementation ***
    fclose(fp);

    // =====================================
    // Connection Teardown: This procedure is provided to you directly and is
    // already working.

    struct packet finpkt, recvpkt;
    buildPkt(&finpkt, ackpkt.acknum, 0, 0, 1, 0, 0, 0, NULL);
    buildPkt(&ackpkt, (ackpkt.acknum + 1) % MAX_SEQN, (ackpkt.seqnum + 1) % MAX_SEQN, 0, 0, 1, 0, 0, NULL);

    printSend(&finpkt, 0);
    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
    timer = setTimer();
    int timerOn = 1;

    double finTimer;
    int finTimerOn = 0;

    while (1) {
        while (1) {
            n = recvfrom(sockfd, &recvpkt, PKT_SIZE, 0, (struct sockaddr *) &servaddr, (socklen_t *) &servaddrlen);

            if (n > 0)
                break;
            if (timerOn && isTimeout(timer)) {
                printTimeout(&finpkt);
                printSend(&finpkt, 1);
                if (finTimerOn)
                    timerOn = 0;
                else
                    sendto(sockfd, &finpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
                timer = setTimer();
            }
            if (finTimerOn && isTimeout(finTimer)) {
                close(sockfd);
                if (! timerOn)
                    exit(0);
            }
        }
        printRecv(&recvpkt);
        if ((recvpkt.ack || recvpkt.dupack) && recvpkt.acknum == (finpkt.seqnum + 1) % MAX_SEQN) {
            timerOn = 0;
        }
        else if (recvpkt.fin && (recvpkt.seqnum + 1) % MAX_SEQN == ackpkt.acknum) {
            printSend(&ackpkt, 0);
            sendto(sockfd, &ackpkt, PKT_SIZE, 0, (struct sockaddr*) &servaddr, servaddrlen);
            finTimer = setFinTimer();
            finTimerOn = 1;
            buildPkt(&ackpkt, ackpkt.seqnum, ackpkt.acknum, 0, 0, 0, 1, 0, NULL);
        }
    }
}
