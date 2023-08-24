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
#include <pthread.h>

#define MAXDATASIZE 300 // max number of bytes we can get at once

#define PORT "3490" // the port users will be connecting to

#define BACKLOG 10 // how many pending connections queue will hold

void sigchld_handler(int s)
{
	// waitpid() might overwrite errno so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

// get sockaddr, IPV4 or IPV6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET){
		return&(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *clientThread(void *client_socket_ptr) {
	int client_socket = *(int *)client_socket_ptr;
	char buffer[MAXDATASIZE];
	int numbytes;

	while(1) {
		numbytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
		if (numbytes <= 0) {
			break;
		}

		buffer[numbytes] = '\0';

		printf("Received message from client: %s\n", buffer);

		if(send(client_socket, "Message received.", 17, 0) == -1){
			perror("send");
		}
	}
	// Implent client to server communcication logic here

	// Close the client socket when done
	close(client_socket);

	pthread_exit(NULL);
}

int main(void)
{
	int sockfd, new_fd; // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	char buffer[MAXDATASIZE];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo("192.168.1.96", PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next){
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) {
		    perror("server: socket");
	            continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
			sizeof(int))== -1) {
			perror("setofsockopt");
			exit(1);
		}	

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
	
		break;

	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

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

	printf("server: waiting for connection...\n");

	while(1) { // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
	}

	// Create a new thread for the client
	pthread_t client_thread;
	if(pthread_create(&client_thread, NULL, clientThread, &new_fd) != 0) {
		perror("pthread_create");
		// Handle error and continue
	}

	inet_ntop(their_addr.ss_family,
		get_in_addr((struct sockaddr *)&their_addr),
		s, sizeof s);
		printf("server: got connection from %s\n", s);
		
		if (!fork()) { // this is the cild process
		   close(sockfd); // child doesnt need the listener			
		   if (send(new_fd, "Hello, world!", 13, 0) == -1)
			perror("send");
		   close(new_fd);
		   exit(0);
		}
		close(new_fd); // parent doesnt need this
	}
	return 0;
}
