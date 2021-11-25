
all: client_compile server_compile 

server_compile:
	gcc -m64 -DSEND_SIZE_MB=1024 server.c -o server 

client_compile: 
	gcc -m64 -DSEND_SIZE_MB=1024 client.c -o client 

clean: 
	rm server client