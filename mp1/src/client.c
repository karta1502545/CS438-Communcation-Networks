/*
** client.c -- a stream socket client demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

// #define PORT "3490" // the port client will be connecting to 

#define MAXDATASIZE 10000 // max number of bytes we can get at once 

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void parseUrl(const char* url, char* host, int* port, char* path) {
	*port = 80;
	strcpy(path, "/");
    const char *start = strstr(url, "://");
    if (!start) start = url;
    else start += 3; // Move past "://"

	// 4 cases
    const char *slash = strchr(start, '/');
    const char *colon = strchr(start, ':');

	if (colon) {
		strncpy(host, start, colon - start);
		host[colon - start] = '\0';
		sscanf(colon, ":%d", port);
		if (slash) { // host + port + path
			strcpy(path, slash);
		}
	} else {
		if (slash) { // host + port
			strncpy(host, start, slash - start);
			host[slash - start] = '\0';

			strcpy(path, slash);
		} else { // host
			strcpy(host, start);
		}
	}
}

int main(int argc, char *argv[])
{
	int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	if (argc != 2) {
	    fprintf(stderr,"usage: client hostname\n");
	    exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char host[256], path[256];
	int port;
	parseUrl(argv[1], host, &port, path);

	char portStr[6];
	snprintf(portStr, sizeof(portStr), "%d", port);
	if ((rv = getaddrinfo(host, portStr, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s); // af, src, dst, size
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	// send request
	char request[1000];
	printf("argv = %s\n", argv[1]);
	printf("path = %s\n", path);
	printf("port = %d\n", port);
	char hostLine[500];
	if (port != 80) {
    	snprintf(hostLine, sizeof(hostLine), "Host: %s:%d", host, port);
	} else {
		snprintf(hostLine, sizeof(hostLine), "Host: %s", host);
	}
	snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		// "Host: %s%s\r\n" // TODO: server IP
		"%s\r\n"
		"Connection: close\r\n\r\n", path, hostLine);//, host, port);

	printf("%s\n", request);
	// write request
	if(send(sockfd, request, strlen(request), 0) == -1) {
		perror("send");
		close(sockfd);
		exit(1);
	}

	// receive response

	// receive length
	// uint32_t fileLength;
	// if((recv(sockfd, &fileLength, sizeof(fileLength), 0) == -1)) {
	// 	perror("recv1");
	// 	exit(1);
	// }
	// long fileLen = ntohl(fileLength);
	// printf("fileLength = %ul\nfileLen = %ld\n", fileLength, fileLen);
	// if ((numbytes = recv(sockfd, buf, fileLen, 0)) == -1) {
	//     perror("recv2");
	//     exit(1);
	// }
	// buf[numbytes] = '\0';
	// printf("client: received '%s'\n",buf);


	// write buf into file
	char filename[10] = "output";
	FILE *file = fopen(filename, "wb");
	if (!file) {
		perror("Failed to open file");
		exit(1);
	}

	int isHeaderEnd = 0;
	char *fileStart;
	int remainLength;
	memset(buf, 0, sizeof(buf));
	while ((numbytes = recv(sockfd, buf, sizeof(buf), 0)) > 0) {
		// printf("%d\n", numbytes);
		if (!isHeaderEnd) {
			buf[numbytes] = '\0';
			fileStart = strstr(buf, "\r\n\r\n");
			if (fileStart) {
				isHeaderEnd = 1;
				fileStart += 4;
				// printf("----------\n");
				// printf("%s", fileStart);
				// printf("----------\n");
				remainLength = numbytes - (fileStart - buf);
				// printf("%d\n", remainLength);

				// write into file
				if (fwrite(fileStart, 1, remainLength, file) != remainLength) {
					perror("Failed to write file");
					fclose(file);
					close(sockfd);
					exit(1);
				}
			}
		}
	    else {
			// printf("ohno \n");
			if (fwrite(buf, 1, numbytes, file) != numbytes) {
				perror("Failed to write file");
				fclose(file);
				close(sockfd);
				exit(1);
			}
		}
	}

	if (numbytes == -1) {
		perror("recv");
	}

	// fwrite(buf, 1, sizeof(buf), file);
	// if(fwrite(buf, 1, sizeof(buf), file) != numbytes) {
	// 	perror("Failed to write file");
	// 	fclose(file);
	// 	exit(1);
	// }

	printf("write response into output\n");
	fclose(file);
	close(sockfd);

	return 0;
}

