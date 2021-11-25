#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAX 4096*1024
#define PORT 8083
#define SA struct sockaddr

// Function designed for chat between client and server.
void chat(int sockfd)
{
	char buff[MAX];
	int n, r;
    
    printf("Write 'exit' to quit the program. \n");

    fseek(stdin,0,SEEK_END);

	// infinite loop for chat
	for (;;) {
	    char buff[MAX];
		int n, r;
        printf("\nWrite message: ");

		n = 0;
        // copy server message in the buffer
		while (((buff[n++] = getchar()) != '\n')  && n < MAX-1);
		buff[n++] = '\0';

        // if msg contains "Exit" then server exit and chat ended.
		if (strncmp("exit", buff, 4) == 0) {
			printf("Server Exit...\n");
			break;
		}

		// and send that buffer to server
		write(sockfd, buff, n);
      
		// read the server from client and copy it in buffer
		r = read(sockfd, buff, MAX);

		// print buffer which contains the client contents
		printf("\n\tReceived(%d): %s", r, buff);
	}
}
  
int main(int argc, char *argv[])
{
    char function;
    int sockfd, connfd;
    struct sockaddr_in servaddr, cli;
    char buff[20];
    char n; 

    if(argc != 2){
        printf("Usage: ./client <IP-dest>. Example: ./client 127.0.0.1\n");
        return -1;
    }
    printf("IP-dest: %s", argv[1]);

  
    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    printf("sock: %d\n", sockfd);

    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(1);
    }
    else
        printf("Socket successfully created..\n");
    bzero(&servaddr, sizeof(servaddr));
  
    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(argv[1]);
    servaddr.sin_port = htons(PORT);
   
    // connect the client socket to server socket
    if (connect(sockfd, (SA*)&servaddr, sizeof(servaddr)) != 0) {
        printf("connection with the server failed...\n");
        close(sockfd);
        exit(1);
    }
    else
        printf("connected to the server..\n");
  
    
    for(;;){

        printf("What do you want to do:\n\tc : chat function. \n\tq : quit \n\tp : send aschii picture \n\t1 : 1 page test. \n\t2 : 10 page test \n\t3 : 100 page test \n\t4 : 1000 page test \n\t5 : 10000 page test \n\t6 : 100000 page test \n\t7 : 1000000 page test \n\ta : all tests \n");
        sleep(1);
        function = getchar();
        n=0;
		while (((buff[n++] = getchar()) != '\n')  && n < MAX-1);


        switch(buff[0]){

            case 'c': 
                chat(sockfd);
                break;

            case 'q': 
                printf("Closing program...\n");
                close(sockfd);
                exit(0);
                break;

             case 'p': 
                printf("Not yet implemented\n");
                break;

            case '1': 
                printf("Not yet implemented\n");
                break;

            case '2': 
                printf("Not yet implemented\n");
                break;

            case '3': 
                printf("Not yet implemented\n");
                break;

            case '4': 
                printf("Not yet implemented\n");
                break;

            case '5': 
                printf("Not yet implemented\n");
                break;
                
            case '6':
                printf("Not yet implemented\n");
                break;

            case '7': 
                printf("Not yet implemented\n");
                break;

            default:   
                printf("Invalid input: %c. Try again.\n", function);

        }

    }
  
    // close the socket    close(sockfd);
}
