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

#define MB					1000000
#define BUF_SIZE			1024
#define METHOD_BUF_SIZE		20
#define VER_BUF_SIZE		10

#define CONNECT			"CONNECT"
#define GET				"GET"
#define POST			"POST"

int port;
int cache_size; // in megabytes

typedef enum {false, true} bool;

pthread_mutex_t cacheLock;

typedef struct cache_entry{
    char* host;  //identifier for the entry
    char* data;  //the data stored
    int size;    //size of data stored

    struct cache_entry *next;
    struct cache_entry *prev;
} cache_entry;

cache_entry* cache;


//Method Declarations
void ignore_sigpipe();
int Read(int fd, void *ptr, size_t nbytes);
int Write(int fd, void *ptr, size_t nbytes);


/*
    add an item to the cache, dealing with LRU and size overflow
*/
void cache_addItem(char* host, char* data, int size){
    pthread_mutex_lock(&cacheLock);
    cache_entry newItem;

    newItem.host = (char*)malloc(strlen(host));
    strcpy(newItem.host,host);
    newItem.data = (char*)malloc(size);
    memcpy(newItem.data,data, size); //now the data is set.

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
    pthread_mutex_unlock(&cacheLock);
}

/*
    get an item from the cache by searching for a specific hostname
    a pointer to the data will be retured in *data.

    returns 0 if the data is found, pointed to with data, -1 if not present

    This will also handle re-ordering for lru
*/
int cache_getItem(char* host, char *data){
    pthread_mutex_lock(&cacheLock);

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

            pthread_mutex_unlock(&cacheLock);
            return 0;
        }

        traverse = traverse->next;
    }
    pthread_mutex_unlock(&cacheLock);
    return -1;
}


void *parseRequest(void* args){
	int clientfd;
	char buf[BUF_SIZE], method[METHOD_BUF_SIZE], url[BUF_SIZE], path[BUF_SIZE], version[VER_BUF_SIZE];
	char* token;
 
	//record ards and free
	clientfd = ((int*)args)[0];
	free(args);
	
	if((Read(clientfd, buf, BUF_SIZE))<0){
		printf("Error reading from connection: %s\n", strerror(errno));
	}
	
	
	printf("String: %s\n", buf);
	
	token = strtok(buf," ");
	if(token==NULL){perror("could not get method\n");return NULL;}
	strcpy(method,token);
	
	token = strtok(buf," ");
	if(token==NULL){perror("could not get URL\n");return NULL;}
	strcpy(url,token);

	
	
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

/* this function is for passing bytes from origin server to client */
void *forwarder(void* args)
{
	int serverfd, clientfd;
	int byteCount=0,numBytesRead, numBytesWritten= 0;
	char buf1[BUF_SIZE];
	char *buf2 = (char *) malloc(4*BUF_SIZE*sizeof(char));
	clientfd = ((int*)args)[0];
	serverfd = ((int*)args)[1];
	char *url = &((char*)args)[2];
	
	
	while(1) {
		numBytesRead = (int)Read(serverfd, buf1, BUF_SIZE);
		memcpy(buf2, buf1, numBytesRead);
		numBytesWritten = (int)Write(clientfd, buf1, numBytesRead);
		
		if(numBytesRead==0 && buf2!=NULL){
			//EOF
			//Add to the cache!
			cache_addItem(url, buf2, byteCount);
			//url variable was used
			free(args);
			shutdown(clientfd,1);
			break;
		} else if (numBytesRead<0 || numBytesWritten<0) {
			//error!
			perror("numBytesRead or numBytesWritten error!");
			free(buf2);
			free(args);
			break;
		} else if (numBytesRead!=numBytesWritten){
			printf("Read Write mismatch!");
			free(buf2);
			free(args);
			break;
		}
		
		byteCount+=numBytesWritten;
		
		if(buf2!=NULL && byteCount*10 > cache_size*MB){
			//Very big object that takes more than 10% of the cache.
			//Do not cache.
			free(buf2);
			buf2 = NULL;
		} else{
			if(byteCount > 4*BUF_SIZE){
				//double the size of the buffer.
				realloc(buf2, byteCount*2);
			}
			memcpy(buf2 + byteCount - numBytesWritten, buf1, numBytesWritten);
		}
	}
	return NULL;
}

int main(int argc, char* argv[])
{
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

    //setup the cache mutex
    pthread_mutex_init(&cacheLock, NULL);

	//Listen to incoming requests
	while(1) {
		clientlen = sizeof(clientaddr);
		thread_args = malloc(sizeof(int));
		// Accept a new connection from a client here
		connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
		// Connection was accepted!
		// Make new thread to handle this request.
		thread_args[0] = connfd;
		pthread_create(&tid, NULL, parseRequest, thread_args);
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

//Read wrapper for handling EINTR
int Read(int fd, void *ptr, size_t nbytes)
{
	int n;
	while(1){
		if ((n = read(fd, ptr, nbytes)) < 0) {
			if (errno == EINTR) continue;
		}
		break;
	}
	return n;

}

//Reads Line
int Readln(int fd, void *ptr, size_t nbytes)
{
	int n;
	while(1){
		if ((n = read(fd, ptr, nbytes)) < 0) {
			if (errno == EINTR) continue;
		}
		break;
	}
	return n;
//	Example
	
//	int n, rc;
//	char c, *bufp = usrbuf;
//	
//	n = 0;
//	
//	while (n < maxlen-1){
//		if ((rc = rio_read(rp, &c, 1)) == 1) {
//	  n++;
//	  *bufp++ = c;
//	  if (c == '\n')
//		  break;
//		} else if (rc == 0) {
//	  break;    /* EOF */
//		} else
//	  return -1;	  /* error */
//	}
//	*bufp = 0;
//	return n;
//
}

//Write wrapper for handling EINTR
int Write(int fd, void *ptr, size_t nbytes)
{
	int n;
	while(1){
		if ((n = write(fd, ptr, nbytes)) < 0) {
			if (errno == EINTR) continue;
		}
		break;
	}
	return n;
}


