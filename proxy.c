#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <netdb.h>
#include <pthread.h>

#define MB 1000000
#define MAX_LENGTH 300

int port;
int cache_size; // in megabytes

typedef enum {false, true} bool;

typedef struct cache_entry{
    char* host;  //identifier for the entry
    char* data;  //the data stored
    int size;    //size of data stored

    pthread_mutex_t mutex;

    struct cache_entry *next;
    struct cache_entry *prev;
} cache_entry;

cache_entry* cache;

/*
    add an item to the cache, dealing with LRU and size overflow
*/
void cache_addItem(char* host, char* data, int size){
    cache_entry newItem;

    newItem.host = (char*)malloc(strlen(host));
    strcpy(newItem.host,host);
    newItem.data = (char*)malloc(size);
    strcpy(newItem.data,data); //now the data is set.

    newItem.next = cache;
    cache->prev = &newItem;
    cache = &newItem;       //update the head with this new item

    cache_entry *head = cache;
    cache_entry *traverse = cache;

    //see if we need to remove some items from the cache
    long sizeAcc = 0;
    bool freeItems = false;

    while(traverse != NULL){
        sizeAcc += traverse->size;
        if(sizeAcc > MB * cache_size){
            freeItems = true;
        }

        if(freeItems){ //delete the last items after an overflow
            cache_entry *temp = traverse->next;
            free(traverse->data);
            free(traverse->host);
            free(traverse);

            traverse = temp;
        }else {
            traverse = traverse->next;
        }
    }
}

/*
    get an item from the cache by searching for a specific hostname
    a pointer to the data will be retured in *data.

    returns 0 if the data is found, pointed to with data, -1 if not present

    This will also handle re-ordering for lru
*/
int cache_getItem(char* host, char *data){
    cache_entry *traverse = cache;
    cache_entry *head = cache;

    while(traverse!=NULL){
        if(strcmp(host,traverse->host)==0){
            //found the entry
            data = traverse->data;
            if(traverse!=head){
                traverse->prev->next = traverse->next;
                if(traverse->next!=NULL){
                    traverse->next->prev = traverse->prev;
                }

                traverse->next=head;
                traverse->prev=NULL;
                head->prev=traverse;
                cache=traverse; //update the head
            }

            return 0;
        }

        traverse = traverse->next;
    }
    return -1;
}
//Declarations
void ignore_sigpipe();


int connection(int fd){
//
//    int sock;
//    struct sockaddr_in addr;
//    addr.sin_family = PF_INET;
//    addr.sin_addr.s_addr = INADDR_ANY;
//    addr.sin_port = htons(port);
//
//    if((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0){
//        perror("Could not create socket");
//        return -1;
//    }
//    if((bind(sock, (struct sockaddr *) &addr, sizeof(addr))) < 0) {
//        perror("Could not bind socket");
//        return -1;
//    }
//    if((listen(sock,10))<0){
//        perror("Could not listen");
//        return -1;
//    }
//
//    //handle incoming connections
//    int rec;
//    socklen_t length;  //it's just an unsigned int under the hood
//    while(1){
//        if((rec = accept(sock, (struct sockaddr*) &addr, &length)) < 0){
//            perror("Error accepting connection");
//            return -1;
//        }
//
//        pid_t pid = fork();
//        if(pid < 0){
//            perror("Error forking");
//            return -1;
//        }
//        if(pid == 0){
//            //we are in the child process
//            /*
//                Read the HTTP request from the socket and make a connection to the
//                specified server
//
//                then go into a while loop that will maintain a connection until there
//                is no more data to pull or the client connection closes
//            */
//            char buf[MAX_LENGTH];
//            if((read(rec, buf, MAX_LENGTH))<0){
//                printf("Error reading from connection: %s\n", strerror(errno));
//            }
//
//            char* token;
//            char method[MAX_LENGTH], url[MAX_LENGTH], path[MAX_LENGTH], version[MAX_LENGTH];
//
//            printf("String: %s\n", buf);
//
//            token = strtok(buf," ");
//            if(token==NULL){perror("could not get method\n");return -1;}
//            strcpy(method,token);
//
//            token = strtok(buf," ");
//            if(token==NULL){perror("could not get URL\n");return -1;}
//            strcpy(url,token);
//
//
//
//        }
//        if(pid > 0){
//            //we are in the parent process
//        }
//    }
//
    return 0;
}

int main(int argc, char* argv[]){
	int listenfd, connfd;
	socklen_t clientlen;
	struct sockaddr_in clientaddr;
	struct hostent *hp;
	char *haddrp;
	pthread_t tid;
	int *thread_args;

    if(argc != 3) {
		errno = EINVAL;
        perror("Invalid argument size! Run with proxy <port> <buff_size_mb>");
        return EXIT_FAILURE;
    }

    port = atoi(argv[1]);
    cache_size = atoi(argv[2]);

    if(port < 1024 || port > 65535) {
		errno = EINVAL;
        perror("Please use a port number between 1024 and 65535");
        return EXIT_FAILURE;
    }

	//Disabling sigpipe
	ignore_sigpipe();

	//Listen to incoming requests
	while(1) {

		clientlen = sizeof(clientaddr);
		thread_args = malloc(sizeof(int));
		/* Accept a new connection from a client here */
		connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
		/* Connection was accepted! */
		hp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
						   sizeof(clientaddr.sin_addr.s_addr), AF_INET);

		haddrp = inet_ntoa(clientaddr.sin_addr);
		thread_args[0] = connfd;
		pthread_create(&tid, NULL, connection, (void *) thread_args);
		pthread_detach(tid);
	}
	return EXIT_SUCCESS;
}

void ignore(){
	;
}

void ignore_sigpipe()
{
	sigset_t sig_pipe;
	struct sigaction action, old_action;

	action.sa_handler = ignore;
	sigemptyset(&action.sa_mask); /* block sigs of type being handled */
	action.sa_flags = SA_RESTART; /* restart syscalls if possible */

	if (sigaction(SIGPIPE, &action, &old_action) < 0){
		perror("Signal error");
		exit(EXIT_FAILURE);
	}
	if(sigemptyset(&sig_pipe) || sigaddset(&sig_pipe, SIGPIPE)){
		perror("creating sig_pipe set failed");
		exit(EXIT_FAILURE);
	}
	if(sigprocmask(SIG_BLOCK, &sig_pipe, NULL) == -1){
		perror("sigprocmask failed");
		exit(EXIT_FAILURE);
	}

	return;
}


