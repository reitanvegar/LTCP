
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/socket.h>
#include <dlfcn.h>

int socket( int family, int type, int protocol ){
	
	if ( family == AF_INET && type == SOCK_STREAM) 
		type = SOCK_DGRAM; 

	printf("family %d, type %d", family, type);
	
	int (*original_socket_function)(int, int, int);

	original_socket_function = dlsym(RTLD_NEXT, "socket"); 

	return original_socket_function(family, type, protocol);
}
