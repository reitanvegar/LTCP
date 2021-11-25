
/* Linux kernel */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include <linux/uio.h> 
#include <linux/mm.h>
// #include <asm/io.h>
#include <linux/delay.h>
#include <net/tcp.h>
#include <linux/time.h>
/* Genif */
#include "genif.h"
#include "kosif.h"
#include "px_dma_main.h"
#include <linux/hugetlb.h>

#include <asm/tlb.h>
#include <asm/tlbflush.h>

/* 
 * Constants, mostly gotten from the dma example. 
 */
#define PROGRAM_NAME                 "ltcp_rdma_deamon"
#define NO_FLAGS                     0
#define NO_ARG                       NULL
#define NO_OFFSET                    0
#define PRIORITY                     0
#define MODULE_ID                    0x4463
#define LOCAL_ADAPTER_NUMBER         0
#define SEGMENT_SIZE                 64*1024
#define SEMAPHORE_TIMEOUT            HZ*10 
#define MB 1024*1024LL
// TODO: Different sizes.

#define LTCP_MAX_HEADER_SIZE (MAX_TCP_HEADER) // - 52 ? 

/* 
 * Shared segment id of where to find the communication structure between the devices.
 */
#define SHARED_SEGMENT_ID_META_QUEUE 0x123
#define SHARED_SEGMENT_ID_APERTURE 0x124
#define SHARED_SEGMENT_ID_APERTURE_RECV 0x125
#define SHARED_SEGMENT_ID_APERTURE_SEND 0x126

#define LTCP_QUEUE_MSG_SIZE 42
#define SIZE_OF_OTHER_ELEMENTS 20
#define LTCP_MSG_QUEUE_BYTESIZE (SEGMENT_SIZE-SIZE_OF_OTHER_ELEMENTS)
#define LTCP_MSG_QUEUE_SIZE LTCP_MSG_QUEUE_BYTESIZE / LTCP_QUEUE_MSG_SIZE  // Should be rounded down to closest whole number. 



/*
 * 
 */
#define APERTURE_SIZE_QUEUE 128LL*MB 

/* 
 * Max to send per packet. 
 */
#define MAX_SEND 127*MB //Shoudl not be larger than APERTURE_SIZE  // TODO: Can't send more than this size without issues. (should maybe be even closer to 128 )
#define MAX_PAGES_SEND (MAX_SEND / PAGE_SIZE) 

/* Adapter number can be set via kernel module argument. */
__u32 local_adapter_number = LOCAL_ADAPTER_NUMBER;
__u32 remote_node_id;
__u32 local_interrupt_number;


osif_iommu_domain_t iommu_domain;
osif_iova_range_t *aperture_range = NULL;
osif_iova_range_t *aperture_range_recv = NULL;
osif_iova_range_t *aperture_range_temp = NULL;


sci_binding_t sci_binding;
sci_l_interrupt_handle_t lih;
sci_r_interrupt_handle_t rih;
sci_l_segment_handle_t lsh_meta_queue;
sci_r_segment_handle_t rsh_meta_queue;
sci_l_segment_handle_t lsh_aperture;
sci_l_segment_handle_t lsh_aperture_send;
sci_l_segment_handle_t lsh_aperture_recv;
sci_l_segment_handle_t lsh_aperture_recv_segment;
sci_r_segment_handle_t rsh_aperture;
sci_r_segment_handle_t rsh_aperture_recv;
__u64 physical_recv_aperture;
__u64 physical_send_aperture;
__u64 remote_recv_start = 0;
ioaddr_list_t ioaddr_list;

osif_iova_handle_t dma_handle_aperture; 
osif_iova_handle_t dma_handle_aperture_send; 





DEFINE_SEMAPHORE(client_semaphore);

spinlock_t send_queue_lock = {0};
spinlock_t recv_queue_lock = {0};

/* 
 * Should contain information needed for each message to know how large next message is and where it is going. 
 */
struct ltcp_queue_msg {
    __u32 src_ip;
    __u32 dest_ip;
    __u32 src_port; 
    __u32 dest_port; 
    __u32 type; 
    __u64 offset_from_aperture; 
    __u64 size;
};
//TODO: remove? could instead communicate the location of an allocated ringbuffer at the beginning of connection setup.

struct ltcp_ring_queue {
    __u32 start;
    __u32 end; 
    __u32 phys_start;
    __u32 phys_end; 
    __u32 int_no;
    struct ltcp_queue_msg queue[LTCP_MSG_QUEUE_SIZE]; 
};

struct ltcp_conn {
    /* 
     * The remote node will probably need to know this inorder to find the location of the remote iommu ring buffer, but we first try to just send the location together with the metapacket. 
     */
    struct ltcp_ring_queue * send_queue;  /* Local send queue and remotes receive_queue */
    struct ltcp_ring_queue * recv_queue; /* Local receive_queue and remotes send_queue */
};


/*
 * Pointer to the local connection. 
 */
struct ltcp_conn ltcp_connection;


struct proto org_tcp_prot;


