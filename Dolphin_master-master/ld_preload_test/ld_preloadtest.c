#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
int main(){
        int sock;
        sock = socket(AF_INET, SOCK_STREAM, 0);
        return 0;
}

