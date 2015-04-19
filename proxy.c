#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#define MB 1000000
#define MAX_LENGTH 512

int port;
int cache_size; // in megabytes

typedef struct cache_entry{
    char* data;
    int size;

    struct cache_entry *next, *prev;
} cache_entry;


int connection(){
    int sock;
    struct sockaddr_in addr;
    addr.sin_family = PF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0){
        perror("Could not create socket");
        return -1;
    }
    if((bind(sock, (struct sockaddr *) &addr, sizeof(addr))) < 0) {
        perror("Could not bind socket");
        return -1;
    }
    if((listen(sock,10))<0){
        perror("Could not listen");
        return -1;
    }

    //handle incoming connections
    int rec;
    socklen_t length;  //it's just an unsigned int under the hood
    while(1){
        if((rec = accept(sock, (struct sockaddr*) &addr, &length)) < 0){
            perror("Error accepting connection");
            return -1;
        }

        pid_t pid = fork();
        if(pid < 0){
            perror("Error forking");
            return -1;
        }
        if(pid == 0){
            //we are in the child process
            /*
                Read the HTTP request from the socket and make a connection to the
                specified server

                then go into a while loop that will maintain a connection until there
                is no more data to pull or the client connection closes
            */
            char buf[MAX_LENGTH];
            if((read(rec, buf, MAX_LENGTH))<0){
                printf("Error reading from connection: %s\n", strerror(errno));
            }

            char* token;
            char method[MAX_LENGTH], url[MAX_LENGTH], path[MAX_LENGTH], version[MAX_LENGTH];

            printf("String: %s\n", buf);

            token = strtok(buf," ");
            if(token==NULL){perror("could not get method\n");return -1;}
            strcpy(method,token);

            token = strtok(buf," ");
            if(token==NULL){perror("could not get URL\n");return -1;}
            strcpy(url,token);



        }
        if(pid > 0){
            //we are in the parent process
        }
    }

    return 0;
}

int main(int argc, char* argv[]){
    if(argc < 3) {
        perror("not enough inputs!");
        return -1;
    }

    port = atoi(argv[1]);
    cache_size = atoi(argv[2]);

    if(port < 1024 || port > 65535) {
        perror("please use a port number between 1024 and 65535");
        return -1;
    }

    return connection();
}
