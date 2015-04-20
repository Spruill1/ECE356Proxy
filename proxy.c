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
#define MAXTRIES			10
#define BUF_SIZE			8192
#define METHOD_BUF_SIZE		20
#define VER_BUF_SIZE		10

#define CONNECT			"CONNECT"
#define GET				"GET"
#define POST			"POST"
#define HTTP			"http://"
#define HTTPS			"https://"
#define HOST			"Host: "

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
int Readln(int fd, char *ptr, size_t nbytes);
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

int parseUrl(char *host, int *port, char *url){
	char buf[BUF_SIZE], *token;

	if (strncasecmp(url, HTTP, 7) == 0) {
		//remove http
		strcpy(buf, url+7);
		//parse host
		token = strtok(buf, ":/\r\n");
		if(token==NULL){
			printf("Could not parse host token!");
			return -1;
		}
		strcpy(host, token);
		token = strtok(NULL, "/");
		
		if(token==NULL){
			return 0;
		}
		
		if(*token==':'){
			*port = atoi(token+1);
			return 1;
		} else
			return 0;
	}
	return -1;
}


void *parseRequest(void* args){
	int clientfd, numBytesRead, serverPort=-1;
	char buf1[BUF_SIZE], buf2[BUF_SIZE], method[METHOD_BUF_SIZE], url[BUF_SIZE], version[VER_BUF_SIZE], host[BUF_SIZE];
	char* token;
 
	//record ards and free
	clientfd = ((int*)args)[0];
	free(args);
	
	if((numBytesRead=Read(clientfd, buf1, BUF_SIZE))<0){
		printf("Error reading from connection: %s\n", strerror(errno));
		return NULL;
	}
	//Copy array for strtok
	strcpy(buf2, buf1);
	
	printf("Incoming Request: %s\n", buf1);
	//Extract Method
	token = strtok(buf2, " ");
	if(token==NULL){
		printf("Could not parse method token!");
		return NULL;
	}
	strcpy(method, token);
	//Extract url
	token = strtok(NULL, " ");
	if(token==NULL){
		printf("Could not parse URL token!");
		return NULL;
	}
	strcpy(url, token);
	//Extract http version
	token = strtok(NULL, " ");
	if(token==NULL){
		printf("Could not parse version token!");
		return NULL;
	}
	strcpy(version, token);
	
	if(parseUrl(host, &serverPort, url)<0){
		//could not find host in url, find host in request
		char *hostbegin, *hostend, urlbuf[BUF_SIZE];
		strcpy(urlbuf, url);
		if((hostbegin = strcasestr(buf1, HOST)) != NULL){
			//find end of host string
			hostbegin = hostbegin + 6;
			hostend = strstr(hostbegin, "\r\n");
			strlcpy(host, hostbegin, hostend-hostbegin);
		}
		
		//Rebuild url, prepend host
		sprintf(url, "%s%s", host, urlbuf);
	}
	
	if(strncasecmp(method, GET, 3)==0){
		
	}
	return NULL;
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
int Readln(int fd, char *buf, size_t nbytes)
{
	int n, num_read;
	char c, *bufp = buf;
	
	n = 0;
	
	while (n < nbytes-1){
	  if ((num_read = Read(fd, &c, 1)) == 1) {
	  n++;
	  *bufp++ = c;
	  if (c == '\n')
		  break;
		} else if (num_read == 0) {
	  break;    /* EOF */
		} else
	  return -1;	  /* error */
	}
	*bufp = 0;
	return n;

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


