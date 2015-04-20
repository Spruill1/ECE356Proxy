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
#define FIELD_CONN      "Connection: "
#define FIELD_PROXY		"Proxy-Connection: "
#define FIELD_KA		"Keep-Alive: "

#define CLOSE_CONN		"Connection: Close\r\n\r\n";

int port;
int cache_size; // in megabytes

typedef enum {false, true} bool;

pthread_mutex_t cacheLock;

typedef struct cache_entry{
	char* url;  //identifier for the entry
	char* data;  //the data stored
	int size;    //size of data stored
	
	struct cache_entry *next;
	struct cache_entry *prev;
} cache_entry;

cache_entry* cache;


//Method Declarations
void *forwarder(void* args);
void ignore_sigpipe();
int open_connection(char *host, int port);
int Read(int fd, void *ptr, size_t nbytes);
int Readln(int fd, char *ptr, size_t nbytes);
int Write(int fd, void *ptr, size_t nbytes);
void removeField(char *s, char *field);
int canCache(char *s);


/*
 add an item to the cache, dealing with LRU and size overflow
 */
void cache_addItem(char* url, char* data, int size){
	pthread_mutex_lock(&cacheLock);
	cache_entry newItem;
	
	newItem.url = (char*)malloc(strlen(url)+1);
	strcpy(newItem.url,url);
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
			free(traverse->url);
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
int cache_getItem(char* url, char *data, int *size){
	pthread_mutex_lock(&cacheLock);
	
	cache_entry *traverse = cache;
	cache_entry *head = cache;
	
	while(traverse!=NULL){
		if(strcmp(url,traverse->url)==0){
			//found the entry
			data = traverse->data;
			*size = traverse->size;
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
	data = NULL;
	pthread_mutex_unlock(&cacheLock);
	return -1;
}

int parseUrl(char *host, int *port, char *url){
	char buf[BUF_SIZE], *token;
	int httpmethod = -1;
	
	if (strncasecmp(url, HTTP, 7) == 0) {
		//remove http
		strcpy(buf, url+7);
		httpmethod = -1;
		
	} else if (strncasecmp(url, HTTPS, 8) == 0) {
		//remove https
		strcpy(buf, url+8);
		httpmethod = 2;
	}
	//parse host
	token = strtok(buf, ":/\r\n");
	if(token==NULL){
		printf("Could not parse host token!");
		return -1;
	}
	strcpy(host, token);
	
	//parse port
	token = strtok(NULL, "/");
	
	if(token==NULL){
		return 0;
	}
	
	if(*token==':'){
		*port = atoi(token+1);
	} else if(httpmethod>0)
		*port = 443;
	else
		*port = 80;
	return 1;
}
void cleanup(int serverfd, int clientfd){
	shutdown(clientfd, 1);
	if(serverfd>0)
		shutdown(serverfd, 1);
}

void *parseRequest(void* args){
	int clientfd, serverfd, numBytesRead, numBytesWritten, serverPort=-1, tries=0, dataSize=-1;
	char buf1[BUF_SIZE], buf2[BUF_SIZE], method[METHOD_BUF_SIZE], url[BUF_SIZE], version[VER_BUF_SIZE], host[BUF_SIZE], *token, *data, *thread_args;
	pthread_t tid;

 
	//record ards and free
	clientfd = ((int*)args)[0];
	free(args);
	
	if((numBytesRead=Read(clientfd, buf1, BUF_SIZE))<0){
		printf("Error reading from connection: %s\n", strerror(errno));
		cleanup(serverfd, clientfd);
		return NULL;
	}
	//Copy array for strtok
	strcpy(buf2, buf1);
	
	printf("Incoming Request: %s\n", buf1);
	//Extract Method
	token = strtok(buf2, " ");
	if(token==NULL){
		printf("Could not parse method token!");
		cleanup(serverfd, clientfd);
		return NULL;
	}
	strcpy(method, token);
	//Extract url
	token = strtok(NULL, " ");
	if(token==NULL){
		printf("Could not parse URL token!");
		cleanup(serverfd, clientfd);
		return NULL;
	}
	strcpy(url, token);
	//Extract http version
	token = strtok(NULL, " ");
	if(token==NULL){
		printf("Could not parse version token!");
		cleanup(serverfd, clientfd);
		return NULL;
	}
	strcpy(version, token);
	
	if(parseUrl(host, &serverPort, url)<=0){
		//could not find host in url, find host in request
		char *hostbegin, *hostend, urlbuf[BUF_SIZE];
		if((hostbegin = strcasestr(buf1, HOST)) != NULL){
			//find end of host string
			hostbegin = hostbegin + 6;
			if((hostend = strstr(hostbegin, "\r\n"))!=NULL)
			   strlcpy(host, hostbegin, hostend-hostbegin);
		}
		//Rebuild url, prepend host
		sprintf(url, "%s%s", host, urlbuf);
	}
	
	if(strncasecmp(method, GET, 3)==0){
		//Get from cache
		if(cache_getItem(url, data, &dataSize)>0){
			//Found data!
			Write(clientfd, data, dataSize);
			shutdown(clientfd, 1);
		}
		//Not in cache, hit server.
		
		//Open connection with server
		while((serverfd = open_connection(host, serverPort))<0){
			tries++;
			if(tries==MAXTRIES){
				printf("Max Number of attempts reached(10)");
				cleanup(serverfd, clientfd);
				return NULL;
			}
		}
		
		//remove unwanted tokens
		removeField(buf1, FIELD_CONN);
		removeField(buf1, FIELD_PROXY);
		removeField(buf1, FIELD_KA);
		
		//Receive the response from server and save it in the cache
		thread_args = (char *)malloc(2*sizeof(int)+strlen(url)+1);
		thread_args[0] = clientfd;
		thread_args[1] = serverfd;
		strcpy(&thread_args[2], url);
		pthread_create(&tid, NULL, forwarder, (void *) thread_args);

		numBytesWritten = Write(serverfd, buf1, strlen(buf1));
		
		if(numBytesWritten<0){
			printf("Get request header write failed!");
			cleanup(serverfd, clientfd);
			return NULL;
		}
		
		//Write rest of the header
		while(1){
			if((numBytesRead=Read(clientfd, buf1, BUF_SIZE))<0){
				printf("Error reading from connection: %s\n", strerror(errno));
				cleanup(serverfd, clientfd);
				return NULL;
			}
			if((numBytesWritten=Write(serverfd, buf1, numBytesRead))<0){
				printf("Error reading from connection: %s\n", strerror(errno));
				cleanup(serverfd, clientfd);
				return NULL;
			}
			if(numBytesRead==0){
				break;
			}
			if(numBytesRead!=numBytesWritten){
				printf("Read Write mismatch!");
				cleanup(serverfd, clientfd);
				return NULL;
			}
		}
		
	}
	
	//cleanup
	cleanup(serverfd, clientfd);
	pthread_join(tid, NULL);
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
		numBytesWritten = (int)Write(clientfd, buf1, numBytesRead);
		
		if(numBytesRead==0 && buf2!=NULL){
			//EOF
			//Add to the cache!
			cache_addItem(url, buf2, byteCount);
			free(buf2);
			//url variable was used
			free(args);
			shutdown(clientfd,1);
			shutdown(serverfd,1);
			break;
		} else if (numBytesRead<0 || numBytesWritten<0) {
			//error!
			perror("numBytesRead or numBytesWritten error!");
			free(buf2);
			free(args);
			shutdown(clientfd,1);
			shutdown(serverfd,1);
			break;
		} else if (numBytesRead!=numBytesWritten){
			printf("Read Write mismatch!");
			free(buf2);
			free(args);
			shutdown(clientfd,1);
			shutdown(serverfd,1);
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
	shutdown(clientfd,1);
	shutdown(serverfd,1);
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

int open_connection(char *host, int port)
{
	int targetfd;
	struct hostent *hp;
	struct sockaddr_in serveraddr;
	
	if ((targetfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1; /* check errno for cause of error */
	
	/* Fill in the server's IP address and port */
	if ((hp = gethostbyname(host)) == NULL)
		return -2; /* check h_errno for cause of error */
	bzero((char *) &serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char *)hp->h_addr,
		  (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);
	
	/* Establish a connection with the server */
	if (connect(targetfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
		return -1;
	return targetfd;
}

void replaceField(char *s, char *field, char *value){
	int len;
	char *endline;
	if((s=strstr(s,field))!=NULL)
		endline = strstr(s, "\r\n");
	len = endline-s+2;
	memmove(s,s+len, strlen(s)-len+1);

}

void removeField(char *s, char *field)
{
	int len;
	char *endline;
	if((s=strstr(s,field))!=NULL)
		endline = strstr(s, "\r\n");
	len = endline-s+2;
	memmove(s,s+len, strlen(s)-len+1);
}

int canCache(char *s){
	if((s=strstr(s,"no-cache"))!=NULL){
		return -1; //NO!
	}
	return 1;
}


