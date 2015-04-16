#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

#define MB 1000000

int port;
int cache_size; // in megabytes

char *cache;

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

    cache = (char*)malloc(cache_size*MB);

    return connection();
}
