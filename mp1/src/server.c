/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

// #define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// long get_file_size(FILE *file) {
//     long fileSize;

//     fseek(file, 0, SEEK_END);
//     fileSize = ftell(file);
//     fseek(file, 0, SEEK_SET);
    
//     return fileSize;
// }

int main(int argc, char *argv[])
{
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	char portStr[6];
	snprintf(portStr, sizeof(argv[1]), "%s", argv[1]);

	if ((rv = getaddrinfo(NULL, portStr, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");
	// HTTP connection
	// 1. 200 for OK / 404 for not found / 400 for other
	// 2. GET relative path
	// 3. ensure 2 \r\n in the end of the msg

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			
			// receive request into buffer
			unsigned char buffer[1000];
			// buffer size should be large enough, or the request may be rejected by server.
			ssize_t bytes_read = read(new_fd, buffer, sizeof(buffer) - 1);
			if (bytes_read < 0) {
				perror("read");
				exit(EXIT_FAILURE);
			}
			buffer[bytes_read] = '\0';
			printf(buffer);

			char *method, *uri, *http_version;

			// get file path
			method = strtok(buffer, " "); // "GET"
			uri = strtok(NULL, " "); // "/example.html"
			uri++; // get rid of starting "/"
			// http_version = strtok(NULL, "\r\n"); // "HTTP/1.1"

			// printf("Method: %s\n", method);
			// printf("URI: %s\n", uri); // Now prints "example.html"
			// printf("HTTP Version: %s\n", http_version);

			char *header;
			// check if file exists
			// printf("Method: %s\n", method);
			// printf("-------\n");
			if (strncmp(method, "GET", 3) == 0) {
				if (access(uri, F_OK) == 0)
					header = "HTTP/1.1 200 OK\r\n\r\n";
				else
					header = "HTTP/1.1 404 Not Found\r\n\r\n";
			} else {
				header = "HTTP/1.1 400 Bad Request\r\n\r\n";
			}

			// send header
			send(new_fd, header, strlen(header), 0);

			// open file
			FILE *file = fopen(uri, "rb");
			if(!file) {
				perror("Failed to open file");
				return -1;
			}

			// get and send length (no need for HTTP)
			// long fileSize = get_file_size(file);
			// printf("fileSize = %ld\n", fileSize);
			// uint32_t fileLen = htonl(fileSize);
			// if (send(new_fd, &fileLen, sizeof(fileLen), 0) == -1)
			// 	perror("send");

			// get and send file
			size_t bytesRead;
			char send_buffer[1000] = {0};
			while ((bytesRead = fread(send_buffer, 1, sizeof(send_buffer), file)) > 0) {
				size_t totalSent = 0;
				ssize_t bytesSent;
				while (totalSent < bytesRead) {
					bytesSent = send(new_fd, send_buffer + totalSent, bytesRead - totalSent, 0);
					if (bytesSent == -1) {
						perror("send file chunk");
						fclose(file);
						close(sockfd);
						return -1;
					}
					totalSent += bytesSent;
				}
			}
			fclose(file);
			close(new_fd);
			exit(0);
		}
		close(new_fd);  // parent doesn't need this
	}

	return 0;
}

