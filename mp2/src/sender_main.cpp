#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <unordered_map>
#include <errno.h>
#include <iostream>

using namespace std;
#define MSS 1400
#define TIMEOUT 40
#define CWND_INIT 10
#define ratio 0.5

// #define PRINT

enum CongestionState {
    SLOW_START = 0,
    CONGESTION_AVOIDANCE = 1,
    FAST_RECOVERY = 2,
    INIT = 3
};

struct sockaddr_in si_other;
socklen_t si_other_len = sizeof(si_other);
int s, slen;
int base;
bool last_ack = false;
float thresh;
float cwnd;


void diep(char *s) {
    perror(s);
    exit(1);
}

typedef struct Packet // one chuck has MSS bytes
{
    long long seq;
    long length;
    char data[MSS];
} packet;

unordered_map<int, packet*> packets;

void printPackets(const std::unordered_map<int, packet*>& packets) {
    cout << "------------------" << endl;
    if (packets.empty()) {
        printf("No packets to display.\n");
        return;
    }

    for (int i = 0; i < packets.size(); i++) {
        auto it = packets.find(i);
        if (it != packets.end()) {
            const packet* pkt = it->second;
            printf("Packet %lld: length %ld\n", pkt->seq, pkt->length);
            printf("Data: ");
            for (long j = 0; j < pkt->length; ++j) {
                printf("%c", pkt->data[j]);
            }
            printf("\n\n");
        } else {
            printf("Packet %d not found.\n", i);
        }
    }
    cout << "------------------" << endl;
}

void* getAck(void*)
{
    int ack_num;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = TIMEOUT * 1000;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1)
        diep("getAck: set tp error");
    thresh = 50; // TODO: can be changed, it means after CWND = 50, we additively increase the congestion window
    int duplicatedAckCount;
    CongestionState state = INIT;

    while (true) {
        // dealing with ack
        int numbytes = recvfrom(s, &ack_num, sizeof(ack_num), 0, (struct sockaddr *)&si_other, &si_other_len);
        if (numbytes == -1 && errno == EAGAIN) {
            // timeout -> resend
            #ifdef PRINT
            cout << "TIMEOUT, ack_num = " <<  ack_num << endl;
            #endif

            state = SLOW_START;
            cwnd = CWND_INIT;
            duplicatedAckCount = 0;
            thresh = cwnd * ratio;

            const packet* pkt = packets[base];
            if (send(s, pkt, sizeof(*pkt), 0) == -1) {
                perror("getAck: send error");
                exit(EXIT_FAILURE);
            }
        }
        if (numbytes > 0) {
            // receive an ACK
            #ifdef PRINT
            // cout << "Received ack " << ack_num << endl;
            #endif

            if (ack_num == -1) {
                #ifdef PRINT
                cout << "Received last ack!" << endl;
                #endif
                last_ack = true;
                return NULL;
            }
            
            // base = max(base, ack_num + 1);
            // TODO: lastAcked, lastSent, window_size, 
        }
        // adjust cwnd
        switch (state) {
            case INIT:
                state = SLOW_START;
                cwnd = CWND_INIT;
                thresh = 200;
                duplicatedAckCount = 0;
                base = ack_num + 1;
                
                #ifdef PRINT
                cout << "INIT: base = " << base << endl;
                #endif
            case SLOW_START:
                if (cwnd >= thresh) {
                    state = CONGESTION_AVOIDANCE;
                }
                if (base < ack_num) { // new ACK
                    cwnd += 1;
                    duplicatedAckCount = 0;
                    base = ack_num + 1;
                    #ifdef PRINT
                    cout << "SLOW_START: new ACK!" << endl;
                    #endif
                } else { // duplicated ACK
                    duplicatedAckCount += 1;
                    #ifdef PRINT
                    cout << "SLOW_START: Duplicated ACK!" << endl;
                    #endif
                    
                }
                if (duplicatedAckCount == 3) { // Fast Recovery
                    thresh = cwnd * ratio;
                    cwnd = thresh + 3;
                    state = FAST_RECOVERY;

                    // resend
                    const packet* pkt = packets[ack_num+1];
                    if (send(s, pkt, sizeof(*pkt), 0) == -1) {
                        perror("SLOW_START: resend error");
                        exit(EXIT_FAILURE);
                    }
                }
                break;
            case CONGESTION_AVOIDANCE:
                if (base <= ack_num) { // new ACK
                    cwnd += 1 / cwnd;
                    duplicatedAckCount = 0;
                    base = ack_num + 1;
                } else { // duplicated ACK
                    duplicatedAckCount += 1;
                }
                if (duplicatedAckCount == 3) { // Fast Recovery
                    thresh = cwnd * ratio;
                    cwnd = thresh + 3;
                    state = FAST_RECOVERY;

                    // resend
                    const packet* pkt = packets[ack_num+1];
                    if (send(s, pkt, sizeof(*pkt), 0) == -1) {
                        perror("CONGESTION_AVOIDANCE: resend error");
                        exit(EXIT_FAILURE);
                    }
                }
                break;
            case FAST_RECOVERY:
                if (base <= ack_num) { // new ACK
                    cwnd = thresh;
                    duplicatedAckCount = 0;
                    base = ack_num + 1;
                    state = CONGESTION_AVOIDANCE;
                } else { // duplicated ACK
                    cwnd += 1;
                    #ifdef PRINT
                    cout << "FAST_RECOVERY" << endl;
                    #endif
                    // TODO: transmit new segment?
                }
                break;
            default:
                #ifdef PRINT
                cout << "Unknown state" << endl;
                #endif
                break;
        }
    }
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */

    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    if (connect(s, (struct sockaddr *)&si_other, sizeof(si_other)) < 0) {
        perror("Connect failed");
        return;
    }

    // read file
    long long nowSeq = 0;
    long remaining_bytes = bytesToTransfer;
    
    while(!feof(fp)) {
        cout << "remaining_bytes: " << remaining_bytes << endl;
        packet *pkt = (packet*) malloc(sizeof(packet));
        pkt->seq = nowSeq++;
        if (remaining_bytes >= MSS)
            pkt->length = fread(pkt->data, 1, MSS, fp);
        else {
            pkt->length = fread(pkt->data, 1, size_t(remaining_bytes), fp);
            // cout << "remain: " << size_t(remaining_bytes) << endl;
        }
        remaining_bytes -= pkt->length;
        // cout << accumulated_bytes << endl;
        // cout << pkt->length << endl;

        if (pkt->length > 0) {
            packets[pkt->seq] = pkt;
        } else {
            free(pkt); break;
        }

        if (remaining_bytes <= 0) break;
    }
    fclose(fp);

    // add FIN packet
    packet *finPkt = (packet*) malloc(sizeof(packet));
    finPkt->seq = -1;
    finPkt->length = 0;
    memset(finPkt->data, 0, MSS);
    packets[nowSeq] = finPkt;

    #ifdef PRINT
    // printPackets(packets);
    #endif

    // send packet
    int idx = 0;
    pthread_t pThread;
    base = 0;

    pthread_create(&pThread, NULL, &getAck, NULL);
    #ifdef PRINT
    cout << "getAck starts." << endl;
    #endif

    while (true) {
        if (last_ack) break;
        // if (idx == packets.size()) break;
        int limit = base + (int) cwnd;
        // cout << "limit = " << limit << ", idx = " << idx << ", base = " << base << endl;

        if (idx < packets.size() && idx < limit) {
            const packet* pkt = packets[idx];
            // cout << "idx = " << idx << ", pkt seq = " << pkt->seq << endl;
            if (send(s, pkt, sizeof(*pkt), 0) == -1) {
                perror("send error");
                exit(EXIT_FAILURE);
            }
            idx++;
        }
    }
    
	/* Send data and receive acknowledgements on s*/

    // pthread_join(pThread, NULL);
    printf("Closing the socket\n");
    close(s);
    return;

}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


