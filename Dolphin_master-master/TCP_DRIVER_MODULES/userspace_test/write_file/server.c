#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/resource.h>

#define MB 1024*1024LL

#define PORT 8083
#define SA struct sockaddr 



//#define SEND_SIZE_MB 
#define SEND_SIZE SEND_SIZE_MB*MB


// static long long SEND_SIZE_MB;
// static long long SEND_SIZE;

// void recvfile(char * filename, int fd){
// 	FILE * fp;
//     // char tempbuff[SEND_SIZE]; 	  //Doesn't work.
//     char tempbuff[SEND_SIZE] = {0}; //Works.
//     fp = fopen(filename, "w+");
//     int i;
//     int r;

//     // read the message from client and copy it in buffer
//     r = read(fd, tempbuff, SEND_SIZE);

//     for(i =0; i < r; i++){
// 		fputc((int)tempbuff[i], fp);
//     } 
//     // print buffer which contains the client contents
//     printf("\n\tRecv(%d)\n", r);
// 	fclose(fp);
// }



void pong(int sockfd, struct sockaddr_in cli, int len){
    //char * tempbuff = (char *) calloc(1, SEND_SIZE); 
    unsigned char tempbuff[SEND_SIZE] = {0}; //Works.

    size_t r, i, tot;
	int fd;
	

	fd = accept(sockfd, (SA*)&cli, &len);
	if (fd < 0) {
		printf("server accept failed...\n");
		exit(0);
	}

    read(fd, tempbuff, 5);
	strcpy(tempbuff, "pong");
    write(fd, tempbuff, 5);
	printf("%s\n", tempbuff);
 
    // sleep(1);

	
	for(i = 1; i < SEND_SIZE; i=i*2){
        tot=0;
        while(r = read(fd, &(tempbuff[i-1]), i)){
            tot += r;     
            if(tot >= i)
                break;
        } 
		write(fd, &(tempbuff[i-1]), i);
    }

	close(fd);
}


// Driver function

int main(int argc, char *argv[])
{
	int sockfd, len;
	struct sockaddr_in servaddr, cli;
	struct rlimit rlim;

	if(argc != 1){
        printf("Usage: ./server \n");
        return -1;
    }


	// SEND_SIZE_MB = (long long) atoi(argv[1]);
	// SEND_SIZE = SEND_SIZE_MB * MB;
    getrlimit(RLIMIT_STACK, &rlim);
	rlim.rlim_cur = (SEND_SIZE_MB + 4) * MB; 
    setrlimit(RLIMIT_STACK, &rlim);

	// socket create and verification
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		printf("socket creation failed...\n");
		exit(0);
	}
	else
		printf("Socket successfully created..\n");
	bzero(&servaddr, sizeof(servaddr));


	// assign IP, PORT
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(PORT);

	// Binding newly created socket to given IP and verification
	if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
		printf("socket bind failed...\n");
		exit(0);
	}
	else
		printf("Socket successfully binded..\n");

	// Now server is ready to listen and verification
	if ((listen(sockfd, 5)) != 0) {
		printf("Listen failed...\n");
		exit(0);
	}
	else
		printf("Server listening..\n");
	len = sizeof(cli);

	// Accept the data packet from client and verification

	// else
	// 	printf("server accept the client...\n");

	// Function for chatting between client and server
	// recvfile("testfil2.txt", connfd);
	pong(sockfd, cli, len);



    // while(1);  
	// After chatting close the socket
	close(sockfd);
}