#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

int port;
int cache_size;

int main(int argc, char* argv[]){
    if(argc < 3) {
        perror("not enough inputs!");
        return -1;
    }

    port = atoi(argv[1]);
    cache_size = atoi(argv[2]);
}
