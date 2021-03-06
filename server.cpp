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
#include <pthread.h>
#include <vector>

#include "alloc.hpp"

#define PORT "3490"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}

typedef struct node{
    char txt[1024];
    struct node* next;
}*pnode;

void free_stack(pnode*);

pnode myStack = NULL;
pthread_mutex_t lock;
std::vector<pthread_t> tids;

void signal_handler(int sig) {
	if(sig == SIGINT) {
		printf("waiting for clients to terminate\n");
		for(int i = 0; i < tids.size(); i++) {
			pthread_join(tids.at(i), NULL);
		}
		printf("freeing stack\n");
		free_stack(&myStack);
		pthread_mutex_destroy(&lock);
		exit(0);
	}
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
};

char* top(pnode head){
    if(head == NULL) {
        printf("ERROR: cant top when stack is empty!\n");
        return NULL;
    }
    else {
        return head->txt;
    }
}

void pop(pnode *head){
    if(*head == NULL) {
        printf("ERROR: cant pop empty stack!\n");
    }
    else {
        pnode temp = *head;
        *head = temp->next;
        my_free(temp);
    }
}

void push(pnode *head, char* text){
    pnode new_node = (pnode)my_malloc(sizeof(struct node));
    if(new_node == NULL) {
        printf("ERROR: no more memory sorry!\n");
        return;
    }
    strcpy(new_node->txt, text);
    new_node->next = *head;
    *head = new_node;
}

void free_stack(pnode *head) {
    pnode curr = *head;
	pnode temp;
    while(curr != NULL) {
        temp = curr;
        curr = curr->next;
        my_free(temp);
    }
	temp = NULL;
	head == NULL;
}

// Here is the function we made that will be run when a thread is created when someone connects to the server
void* client_thread(void* sock_fd) {
	// if (send(*(int *)sock_fd, "Hello, world!", 13, 0) == -1) { // send to the client "Hello, world!"
	// 	perror("send");
	// }
    int fd = *(int*)sock_fd;
    char msg[1030];
    while(1) {
        if(recv(fd, msg, 1030, 0) < 0){
            printf("ERROR: on recieve");
        }
        else {
            if(strncmp(msg, "EXIT", 4) == 0) {
				printf("recieved EXIT!\n");
                close(fd); // close the file descripter for this client
				pthread_exit(NULL); // exit this thread
            }
            else if(strncmp(msg, "PUSH", 4) == 0){
				pthread_mutex_lock(&lock);
                push(&myStack, msg+5);
				pthread_mutex_unlock(&lock);
            }
            else if(strncmp(msg, "POP", 3) == 0){
				pthread_mutex_lock(&lock);
                pop(&myStack);
				pthread_mutex_unlock(&lock);
            }
            else if(strncmp(msg, "TOP", 3) == 0){
				pthread_mutex_lock(&lock);
                char *text = top(myStack);
				pthread_mutex_unlock(&lock);
                if(text != NULL) {
                    if(send(fd, text, 1025, 0) < 0) {
                        perror("ERROR: sending");
                    }
                }
                else {
                    if(send(fd, "<ERROR: stack is empty>", sizeof("<ERROR: stack is empty>"), 0) < 0) {
                        perror("ERROR: sending");
                    }
                }
            }
        }
    }
	// close(fd); // close the file descripter for this client
	// pthread_exit(NULL); // exit this thread
}

int main(void)
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

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
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

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
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

	printf("server: waiting for connections...\n");

	signal(SIGINT, &signal_handler);

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("server: got connection from %s\n", s);

		pthread_t ptid;
		pthread_create(&ptid, NULL, &client_thread, (void *)&new_fd); // creates a thread that runs the function "client_thread"
		tids.push_back(ptid);
		// if (!fork()) { // this is the child process
		// 	close(sockfd); // child doesn't need the listener
		// 	if (send(new_fd, "Hello, world!", 13, 0) == -1)
		// 		perror("send");
		// 	close(new_fd);
		// 	exit(0);
		// }
		// close(new_fd);  // parent doesn't need this
	}

	return 0;
}

