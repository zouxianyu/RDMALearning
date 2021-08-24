#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <time.h>

#define PORT 34567
#define MAX_SIZE 0x100000
#define WARMUP_REP 100 // cycle sufficient times to warm up
#define TRANSMISSION_REP 10000 // a number much greater than 1, so we can ignore the reply

ssize_t send_all(int sockfd, const void *buff, size_t nbytes, int flags)
{
    int i = 0;
    do{
        int ret = send(sockfd, buff + i, nbytes - i, flags);
        if(ret < 0)
            return ret;
        i+= ret;
    }while(i != nbytes);
    return i;
}

ssize_t recv_all(int sockfd, void *buff, size_t nbytes, int flags)
{
    int i = 0;
    do{
        int ret = recv(sockfd, buff + i, nbytes - i, flags);
        if(ret < 0)
            return ret;
        i+= ret;
    }while(i != nbytes);
    return i;
}

int main()
{
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = PF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1){
        printf("%s\n", "bind error");
        goto bad_init;
    }

    if(listen(sockfd, 10) == -1){
        printf("%s\n", "listen error");
        goto bad_init;
    }


    struct sockaddr_in client_addr;
    socklen_t length = sizeof(client_addr);
    int conn = 0;
    if((conn = accept(sockfd, (struct sockaddr*)&client_addr, &length)) < 0){
        printf("%s\n", "accept error");
        goto bad_init;
    }

    // start tests
    void *buffer = malloc(MAX_SIZE);

    int cur_size;
    for(cur_size = 1; cur_size <= MAX_SIZE; cur_size*= 2){

        int i;

        // warm up the cache and TLB
        i = WARMUP_REP;
        while(i--){
            memset(buffer, 0, cur_size);
        }

        // start transmission
        i = TRANSMISSION_REP;
        int length = 0;
        while(i--){
            if((length = recv_all(conn, buffer, cur_size, 0)) != cur_size){
                printf("%s\n", "transmit error");
                goto bad_transmit;
            }
        }
        if((length = send_all(conn, buffer, cur_size, 0)) != cur_size){
            printf("%s\n", "ack error");
            goto bad_transmit;
        }
    }
    

good:
    close(conn);
    return 0;

bad_transmit:
    close(conn);
bad_init:
    close(sockfd);
    return 1;
}