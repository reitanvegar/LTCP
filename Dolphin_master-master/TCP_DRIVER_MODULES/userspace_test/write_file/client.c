#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>       // for clock_t, clock(), CLOCKS_PER_SEC
#include <sys/resource.h>

#define MB 1024*1024LL
#define PORT 8083
#define SA struct sockaddr
#define BILLION  1000000000L

#define SEND_SIZE SEND_SIZE_MB*MB
static struct timespec start, stop;

// static long long SEND_SIZE_MB;
// static long long SEND_SIZE;

// //send a file
// void sendfile(char * filename, int fd){
//     char tempbuff[SEND_SIZE];
//     FILE* fp;
//     fp = fopen(filename, "r");
//     int i;
//     int c; 
//     i=0;  
//     while((c = fgetc(fp)) != EOF){
//         tempbuff[i++]=(char) c; 
//     } 
//     tempbuff[i]='\0';

//     printf("Send(%d)\n", i);
// 	write(fd, tempbuff, i);
//     fclose(fp);
// }
void ping(int fd, char * addr){

    //char * tempbuff = (char *) calloc(1,SEND_SIZE); 
    unsigned char tempbuff[SEND_SIZE] = {0}; //Works.
    // char tempbuff[SEND_SIZE]; //Works.
    size_t r, tot;
    int i, j;
    clock_t begin, end;
    // to store the execution time of code
    struct sockaddr_in servaddr;
    double time_spent = 0.0;

    bzero(&servaddr, sizeof(servaddr));
  
    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(addr);
    servaddr.sin_port = htons(PORT);
   
   	strcpy(tempbuff, "ping");
    
    clock_gettime( CLOCK_REALTIME, &start);
    // connect the client socket to server socket
    if (connect(fd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        close(fd);
        exit(1);
    }
    // else
    //     printf("connected to the server..\n");

    

    // just to make sure connection is setup already as we can move that to connect function.
    write(fd, tempbuff, 5);
    read(fd, tempbuff, 5);
    

    clock_gettime( CLOCK_REALTIME, &stop);
    time_spent = ( stop.tv_sec - start.tv_sec ) + (double)( stop.tv_nsec - start.tv_nsec ) / (double)BILLION;
    printf("%lf\n", time_spent);


    printf("%s\n", tempbuff);

    sleep(1);
    

    for(i=1;i<SEND_SIZE;i=i*2){
        
        /* 
         * For each MB.
         */
        for(j=0; j*MB<i; j++){
            tempbuff[j*MB] = (char)(j%256);
        }

        clock_gettime( CLOCK_REALTIME, &start);
        write(fd, tempbuff, i);
        tot=0;

        while(r = read(fd, &(tempbuff[i-1]), i)){
            tot += r;     
            if(tot >= i)
                break;
        } 
        clock_gettime( CLOCK_REALTIME, &stop);
        time_spent = ( stop.tv_sec - start.tv_sec ) + (double)( stop.tv_nsec - start.tv_nsec ) / (double)BILLION;

        printf("%d,%lf\n", i, time_spent);
 
        for(j=0; j*MB<i; j++){
            printf("%d\n", (int)tempbuff[j*MB]);
        }
    }
    
 

    // printf("RTT time: %lf\n", time_spent);
    // printf("Recv(%ld)\n", tot);
    sleep(1);
}


int main(int argc, char *argv[])
{
    char function;
    int sockfd;
    struct rlimit rlim;
    if(argc != 2){
        printf("Usage: ./client <IP-dest>. Example: ./client 127.0.0.1  1 \n");
        return -1;
    }
    printf("IP-dest: %s", argv[1]);


	// SEND_SIZE_MB = (long long) atoi(argv[2]);
	// SEND_SIZE = SEND_SIZE_MB * MB;


    getrlimit(RLIMIT_STACK, &rlim);
	rlim.rlim_cur = (SEND_SIZE_MB + 4) * MB;
    setrlimit(RLIMIT_STACK, &rlim);

  
    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    printf("sock: %d\n", sockfd);

    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(1);
    }
    else
        printf("Socket successfully created..\n");


    //sleep(1);
    ping(sockfd, argv[1]);

    // sendfile("testfile.txt", sockfd);
    // while(1);   
}
