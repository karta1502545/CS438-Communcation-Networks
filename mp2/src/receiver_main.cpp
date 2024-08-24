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
#include <unordered_map>
#include <iostream>

using namespace std;

struct sockaddr_in si_me, si_other;
int s;
socklen_t slen;
// #define MAXDATASIZE 3000 // max number of bytes we can get at once 
#define MSS 1400
// #define PRINT

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
long long nextSeqExpected = 0;
long long lastSeqReceived = 0; // TODO: init value may not set properly

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    remove(destinationFile);
    slen = sizeof (si_other);

    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    #ifdef PRINT
    printf("Now binding\n");
    #endif
    if (bind(s, (struct sockaddr*) &si_me, sizeof (si_me)) == -1)
        diep("bind");

    FILE *file = fopen(destinationFile, "wb");
    if (file == NULL) {
        diep("file open error");
    }

    int counter = 0;

	/* Now receive data and send acknowledgements */
    while (true) {
        if (counter == -1) break;

        #ifdef PRINT
        cout << "nextSeqExpected = " << nextSeqExpected << ", lastSeqReceived = " << lastSeqReceived << endl;
        #endif
        packet* pkt = new packet;
        int numbytes = recvfrom(s, pkt, sizeof(*pkt), 0, (struct sockaddr*)&si_other, &slen);
        if (numbytes == -1) {
            perror("recv");
            exit(1);
        }
        // sleep(1);
        #ifdef PRINT
        printf("Received packet %ld with %ld bytes of data.\n", pkt->seq, pkt->length);
        #endif
        packets[pkt->seq] = pkt;
        lastSeqReceived = max(lastSeqReceived, pkt->seq);
        // find nextSeqExpected
        while(packets.find(nextSeqExpected) != packets.end()) {
            nextSeqExpected++;
        }
        
        int ack_send = nextSeqExpected - 1;
        // reply with ack
        if (nextSeqExpected > lastSeqReceived && pkt->seq == -1)
            ack_send = -1;

        if(sendto(s, &ack_send, sizeof(ack_send), 0, (struct sockaddr *)&si_other, slen) == -1){
            delete pkt;
            diep("send error");
        }

        // write file
        // Append when receiving consecutive packets
        auto it = packets.find(counter);
        while (it != packets.end()) {
            const packet* pkt = it->second;
            fwrite(pkt->data, sizeof(char), pkt->length, file);
            counter += 1;
            it = packets.find(counter);
            // delete pkt;
        }

        if (pkt->seq == -1) {
            // FIN signal // 應該要確認前面所有的ack都收到了才能break
            counter = -1;
        }

        // nextSeqExpected(下個應該要收到的ack) -> base
        // lastSeqReceived(超過receiver的window size的話要丟掉)(我的buffer夠大的話，應該可以不用？)
    }
    //// write file all at once
    // for (int i = 0; i < packets.size(); i++) {
    //     auto it = packets.find(i);
    //     if (it != packets.end()) {
    //         const packet* pkt = it->second;
    //         fwrite(pkt->data, sizeof(char), pkt->length, file);
    //         delete pkt;
    //     }
    // }

    fclose(file);
    packets.clear();
    
    // TODO: ACK timeout -> resend(不需要，sender沒收到ack的話會重新傳packet)

    close(s);
    #ifdef PRINT
    printf("%s received.", destinationFile);
    #endif
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}

