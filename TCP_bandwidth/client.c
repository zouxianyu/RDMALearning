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
#define WARMUP_REP 100
#define TRANSMISSION_REP 1000

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


int main(int argc, char *argv[])
{
    if(argc != 2){
        printf("%s\n", "invalid parameter");
        return 0;
    }

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = PF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("%s\n", "bad connect");
        goto bad;
    }

    // start tests
    void *buffer = malloc(MAX_SIZE);

    int cur_size;
    for(cur_size = 1; cur_size <= MAX_SIZE; cur_size*= 2){

        int i;

        // warm up the cache and TLB
        i = WARMUP_REP;
        while(i--){
            memset(buffer, 0, sizeof(buffer));
        }

        // start transmission
        clock_t start = clock();
        i = TRANSMISSION_REP;
        int length = 0;
        while(i--){
            if((length = send_all(sockfd, buffer, cur_size, 0)) != cur_size){
                printf("%s\n", "bad transmit");
                goto bad;
            }
        }
        if((length = recv_all(sockfd, buffer, cur_size, 0)) != cur_size){
            printf("%s\n", "bad ack");
            goto bad;
        }
        clock_t end = clock();

        // calculate
        double interval = (double)(end - start) / CLOCKS_PER_SEC;
        double throughput = TRANSMISSION_REP * cur_size / interval; // byte per sec
        
        // print
        printf("%d\t", cur_size);
        if(throughput < 1024){
            printf("%lf\t%s\n", throughput, "Byte/Sec");
        }else if(throughput < 1024 * 1024){
            printf("%lf\t%s\n", throughput / 1024, "Kb/Sec");
        }else if(throughput < 1024 * 1024 * 1024){
            printf("%lf\t%s\n", throughput / (1024 * 1024), "Mb/Sec");
        }else{
            printf("%lf\t%s\n", throughput / (1024 * 1024 * 1024), "Gb/Sec");
        }
    }
good:
    close(sockfd);
    return 0;

bad:
    close(sockfd);
    return 1;
}