
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h> 
#include <stdint.h>
#include <sys/epoll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <getopt.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> 
#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <endian.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <errno.h>

#define  MAX_INTERFACE_NAME_SIZE 50
#define IPPROTO_LOCAL_TCP 5

/*
 * This will be the structure of each interface.
 * Unlike IP we will use one MIP address per node, so therefore there is no need for the mip address in this struct.
 */
typedef struct interface {
    char name[MAX_INTERFACE_NAME_SIZE];
    struct sockaddr_ll sockaddr;
}interface;

struct interface * interfaces;
int number_of_interfaces = 0;
int debug_mode = 1;

typedef struct eth_header{
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t proto;
    /*
     * Note: The ethernet protocol field is used to specify the length if the value is 42-1500 (not allowed to be under 42).
     *       Values from 1501 - 1535 are undefined. From 1536 and above it is used to indicate EtherType. 
     *       In the assignment we are using it to indicate EtherType with ETH_P_MIP
     */
}__attribute__((__packed__)) eth_header; 



typedef struct ip_header {
    unsigned int hdr_len:4;
    unsigned int version:4; 
	uint8_t	    tos;			/* type of service */
	uint16_t	pkt_len;			/* total length */
	uint16_t	id;			    /* identification */
	uint16_t    frag_off;		/* first 3 bits are flags last 13 is fragment offset field */
	uint8_t	    ttl;			/* time to live */
	uint8_t	    proto;			/* protocol */
	uint16_t    sum;			/* checksum */
	uint32_t    src;
    uint32_t    dst;	/* source and dest address */
}__attribute__((__packed__)) ip_header;




/*
 * Will send the ethernet frame.
 * frame_hdr: the header of the frame. 
 * packet: the  packet
 * interface: the interface to send the packet out of. 
 *
 * ethernet socket(global): The ethernet socket to send out of.  
 */
void send_ip_packet(int ethernet_socket, struct iphdr ip_hdr, char * packetdata, int packetlen){
    /*
     * Need a message header struct for the sendmsg call. 
     * Init to 0, so we don't have to set NULL on all unused props in the msghdr struct. 
     */
    struct msghdr msg = {0};

    struct sockaddr_in socketaddr = {0};
    socketaddr.sin_addr.s_addr = ip_hdr.daddr;
    socketaddr.sin_family = AF_INET;
    socketaddr.sin_port = 0;

    /*
     * Should specify the msg_name as this is we are sending with a datagram socket. (NIC need to know which ethernet port(interface) to use)
     */
    msg.msg_name = &socketaddr;
    msg.msg_namelen = sizeof(struct sockaddr_in);

  
    /*
     * Specify how much to send, TODO: this could be split up? 
     */
    struct iovec iov[2];
    iov[0].iov_base = &ip_hdr;
    iov[0].iov_len  = (size_t) sizeof(struct iphdr);     
    iov[1].iov_base = packetdata;
    iov[1].iov_len  = (size_t) packetlen; 
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    
    /* 
     * Send ethernet frame 
     */
    if(sendmsg(ethernet_socket, &msg, 0)<0){
        perror("failed to send."); 
    } 

}   


/*
 * Peeks of the ethernet header on the ethernet socket. 
 *
 * ethernet_header: The header to fill. 
 */
void recv_ip_packet(int ethernet_socket, struct iphdr * ip_hdr, char * packetdata, int packetlen){

    /*
     * Need a message header struct for the recvmsg call. 
     * Init to 0, so we don't have to set NULL on all unused props in the msghdr struct. 
     */
    struct msghdr msg = {0};
    
    /*
     * Don't need to specify msg_name for before recvmsg even though this is a data stream.
     * This implicitly mean that the device driver won't need you to specify which physical ethernet port (interface) you are going to receive from.
     * The device driver will fill inn the sockaddr_ll struct to contain link layer information about the frame when received. (even with SOCK_RAW)
     * In that way, we can use the msg_name after the recvmsg call to among other things figure out interface the data was received on. 
     */
    

    /*
     * Specify how much to read and where to put it.
     */
    struct iovec iov[2];
    iov[0].iov_base = ip_hdr;
    iov[0].iov_len  = (size_t) sizeof(struct iphdr); 
    iov[1].iov_base = packetdata;
    iov[1].iov_len  = (size_t) packetlen; 
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    
    /* 
     * Read in the ethernet header. 
     */
    recvmsg(ethernet_socket, &msg, 0); 
}




/*
 * The function will get the number of interfaces.
 * ifaddr: a linked list of interface address structs. 
 */
int get_number_of_interfaces(struct ifaddrs* ifaddr) {
    int num_if_addr = 0; 
    while(ifaddr) {
        if(ifaddr->ifa_addr != NULL && ifaddr->ifa_addr->sa_family == AF_PACKET){
            if(strcmp(ifaddr->ifa_name , "lo") != 0){
                num_if_addr++;
            }
        } 
        ifaddr = ifaddr->ifa_next;
    }
    return num_if_addr;
}




/*
 * This function will get all the interfaces details and add them to the interface array.
 * This is used when starting the program to set up the interface structs. 
 * interfaces(global) : An array of interface struct I have created,
 *  to keep track information related to each interface.
 * 
 */
void setup_interfaces(){
    struct ifaddrs* ifaddr;
    
    /*
     * Will set the ifaddr to point to a linked list of interface address structures. 
     */
    getifaddrs(&ifaddr);

    /* 
     * Check return value.
     */
    if((getifaddrs(&ifaddr)) < 0) perror("Could not get interfaces \n");

    /*
     * Loop over first to get the number of interfaces. 
     */
    number_of_interfaces = get_number_of_interfaces(ifaddr);
    if(debug_mode) printf("Number of interfaces: %d \n", number_of_interfaces);

    /*
     * Allocate space for interface addresses.
     */
    interfaces = calloc(number_of_interfaces, sizeof(struct interface));

    /* 
     * Loop over all the interfaces. 
     */    
    int index = 0;
    while(ifaddr){

        /*Only get the AF_PACKET interfaces. (Low level interfaces)*/
        if(ifaddr->ifa_addr != NULL && ifaddr->ifa_addr->sa_family == AF_PACKET){
            /*Don't get the loop back interface. */
            if(strcmp(ifaddr->ifa_name , "lo") != 0){

                /* 
                * Get the socket address struct for the link layer and add it to the interface array.
                * TODO: is memcpy actually needed here?
                */
                memcpy(&(interfaces[index].sockaddr), ifaddr->ifa_addr, sizeof(struct sockaddr_ll));
            
                strncpy(interfaces[index].name, ifaddr->ifa_name, MAX_INTERFACE_NAME_SIZE);

                if(debug_mode) printf("Found interface: %s\n", ifaddr->ifa_name);


                index++;
            }
        }

        ifaddr = ifaddr->ifa_next;
    }

    /* 
     * getifaddrs will allocate some memory, so need to make sure this memory is freed.
     */
    freeifaddrs(ifaddr);
}


uint16_t calc_checksum(struct iphdr ip_hdr){
    ip_hdr.check = 0; //
    unsigned short * arr = (unsigned short *) &ip_hdr;
    register long temp_sum = 0;
    char i;

    for(i = 0; i<10; i++)
        temp_sum += *arr++;

    while (temp_sum >> 16) 
        temp_sum = (temp_sum >> 16) + (temp_sum & 0xFFFF);

    return ~temp_sum;
}


int main(int argc, char * argv[]){

    int ethernet_socket =  socket(AF_INET, SOCK_RAW, IPPROTO_LOCAL_TCP);
    
    if(ethernet_socket < 0) {
        perror("Failed to create ethernet socket. \n");
    }

    char data[] = "Test string";
    int data_size = strlen(data)+1;

    /* 
     * Ethernet header 
     */
    // eth_header frame_hdr; 
    // uint8_t dst[] = {0x08, 0x00, 0xC0, 0x56, 0x50, 0x00};
    // uint8_t src[] = {0xF8, 0xb2, 0x1d, 0x29, 0x0c, 0x00};
    // memcpy(frame_hdr.dst, dst, 6); 
    // memcpy(frame_hdr.src, src, 6); 
    // frame_hdr.proto = htons(ETH_P_IP);
    
    // ip_header ip_hdr;
    // ip_hdr.version = 4; 
    // ip_hdr.hdr_len = sizeof(ip_header) % 4 ? (sizeof(ip_header)/4)+1 : sizeof(ip_header)/4; 
    // ip_hdr.tos = 0; // default value is. Routine packet. 
    // ip_hdr.pkt_len = htons(ip_hdr.hdr_len * 4 + data_size);
    // ip_hdr.id = 0; //should find the next id. TODO:
    // ip_hdr.frag_off = 0; //
    // ip_hdr.ttl = htons(128);
    // ip_hdr.proto = htons(253); //test protocol
    // ip_hdr.src = inet_addr("192.168.6.133");
    // ip_hdr.dst =inet_addr("192.168.6.134");

    struct iphdr ip_hdr;
    ip_hdr.version = 4; 
    ip_hdr.ihl = 5;//sizeof(struct iphdr) % 4 ? (sizeof(struct iphdr)/4)+1 : sizeof(struct iphdr)/4; 
    ip_hdr.tos = 0; // default value is. Routine packet. 
    ip_hdr.tot_len = htons(ip_hdr.ihl * 4 + data_size);
    ip_hdr.id = 1; //should find the next id. TODO:
    ip_hdr.frag_off = 0; //
    ip_hdr.ttl = 128; //
    ip_hdr.protocol = IPPROTO_LOCAL_TCP; //test protocol
    ip_hdr.saddr = inet_addr("192.168.6.134");
    ip_hdr.daddr = inet_addr("192.168.6.136");
    
    // can't calculate checksum before after we have added all the info. 
    ip_hdr.check = 0;
    calc_checksum(ip_hdr);


    printf("proto: %d\n", IPPROTO_LOCAL_TCP);
    
    // setup_interfaces();

    // /* Broadcast out of all interfaces. */
    // for(int i = 0; i < number_of_interfaces;  i++ ){
    //     send_ip_packet(ethernet_socket, interfaces[i].sockaddr, ip_hdr, data, data_size);
    // }
    
    send_ip_packet(ethernet_socket, ip_hdr, data, data_size);
   
    char data2[] = "Not a test    ";

    recv_ip_packet(ethernet_socket, &ip_hdr, data2, strlen(data) + 1);

    printf("data: %s" ,  data2); 
}