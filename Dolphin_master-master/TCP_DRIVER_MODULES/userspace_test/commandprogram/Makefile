all: client_compile server_compile 

server_compile: 
	gcc  server.c -o server

client_compile: 
	gcc client.c -o client

run_server:
	./server

IP_SRC=$(shell hostname -I)
# IP_DEST=$(shell /opt/DIS/sbin/disinfo -hostnames | while IFS= read -r line ; do\
# 		if [ $$line != $(IP_SRC) ]; then\
# 			IP_DEST="$$line";\
# 			echo "IP dest: $$IP_DEST";\
# 			break;\
# 		fi;\
# 	done;)

clean: 
	rm server client