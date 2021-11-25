#include <linux/module.h>
#include <linux/moduleparam.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/flow.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <net/ip.h>
#include <net/inet_connection_sock.h>
#include <net/inet_common.h>
#include <linux/unistd.h>
#include <net/protocol.h>
#include <linux/fdtable.h>
#include <net/route.h>
#include <linux/err.h>
#include <linux/wait.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>


/*
 * The different types the connection could have.  
 * (new credits doesn't need to be a type just sent together with with any packet)
 */
enum local_tcp_type {
	REGULAR,
	CONNECT,
	CLOSE, 
	CONNECT_SUCCESS
};

/* 
 * The current local tcp header.
 */
struct local_tcp_header {
	__u16 sport; // local port
	__u16 dport; // remote port
	__u32 type : 2;  // TODO: I could potentially create a mapping for the protocol which changes the need for 4 ekstra bytes... Write in thesis. 
	__u32 credits : 30;  // TODO:  remove?                 // If I do as written with type I could also make credits smaller by makeing 1 bit equal to 1000credits, 2 bits 2000, or similar. .. Write in thesis. 
}__attribute__((packed));

/* 
 * 
 */
#define IPPROTO_LOCAL_TCP 5
#define MAX_NUMBER_OF_LOCAL_RDMA_IP 16
#define BYTES_IN_IP_ADDR 16
#define BYTES_IN_IP_ADDR 16
#define MAX_NUMBER_OF_PORTS 65535
#define LTCP_MAX_HEADER_SIZE (MAX_TCP_HEADER) // - 52 ? 


#define KEEP_ALIVE_TIME 10

static unsigned long node_id = 1;
char * src;
char * dest;
module_param(node_id, ulong, 0);
module_param(src, charp, 0);
module_param(dest, charp, 0);

static unsigned long dest_IP = 0; 
static unsigned long src_IP = 0; 


/* 
 * TODO: Care about different interfaces as the will have different IP addresses. 
 */
struct port_sk {
	struct ltcp_sock * sk;
	__u16 rport; // should store the remote port as this isn't stored by inet_sk unless we set it.
	__u32 rip; // should store the remote port as this isn't stored by inet_sk unless we set it.
	struct port_sk* next; //TODO: change this to list_head structure ? 
	struct port_sk* previous;
};

/* Filled with new connections for this port. */
struct port_sk_arr {
	spinlock_t lock;
	struct port_sk * head;
	struct port_sk * tail;
	struct sock * listening_sock;	
};




struct ltcp_sock {
	/*inet_sock has to be first member.*/
	struct inet_sock icsk_inet;
	struct timespec last_update;
	// struct sk_buff_head recv_queue;
};

/*
 * Data structure to keep track of incomming connections.
 */
struct port_sk_arr incoming_conns[MAX_NUMBER_OF_PORTS] = {0};

/*
 * Data structure to keep track of already connected connections.
 */
struct port_sk_arr conns[MAX_NUMBER_OF_PORTS] = {0};

/* 
 * List over local IPs. 
 */
unsigned long list_of_local_ips[MAX_NUMBER_OF_LOCAL_RDMA_IP] = {0};
EXPORT_SYMBOL(list_of_local_ips);


/* 
 * TODO: check if it should be removed
 */
int local_tcp_module_init_function(void);
void local_tcp_module_exit_function(void);

/* 
 * The receive function to receive the socket buffer from the rdma driver.
 */
int local_tcp_rcv(struct sk_buff *skb);

/*
 * TODO: rename to something like rdma_direct_copy.
 */
extern int ltcp_direct_copy(struct inet_sock *sk, struct msghdr *msg, size_t len, __u32 type);


