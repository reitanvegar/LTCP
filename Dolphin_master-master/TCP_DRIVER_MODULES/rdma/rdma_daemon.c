
/* 
 * Include the ltcp protocol function. Should be able to include this through a headerfile and not c file? And should be able to pass implementation of ltcp_functions without extern and issues when insmod?
 */
#include "../ltcp/local_tcp_protocol.c"
/* 
 * Include the header files etc.
 */
// #include "rdma_daemon.h"

/*
 * Get index of a free entry in ringbuffer. 
 * 
 */
struct ltcp_queue_msg* get_free_metamessage(struct ltcp_ring_queue *rq){

    /* 
     * Find the available space of the ring queueu. This par is not really needed if 
     * LTCP_MSG_QUEUE_SIZE % sizeof(struct ltcp_ring_buffer) == 0. Probably more relevant for the IOMMU ringbuffer. 
     */
    if(rq->end+1 == rq->start){
        return NULL;
    }
    
    /* 
     * If it reaches end start from 0. 
     */ 
    if(rq->end+1 == LTCP_MSG_QUEUE_SIZE){
        rq->end = 0;   
    }
    
    return &(rq->queue[rq->end]);     
}

__u32 space_left(struct ltcp_ring_queue *q){
    if(q->phys_end > q->phys_start)
        return q->phys_end - q->phys_start;
    return (APERTURE_SIZE_QUEUE - q->phys_start) + q->phys_end;
}
/* 
 * Print the ltcp_header.
 */
void print_ltcp_header(char * str, struct ltcp_queue_msg * ltcp_msg){
      printk("\n%s:\n\tsrc-ip\t\t\t%d\n\tdest-ip\t\t\t%d\n\tsrc-port\t\t%d\n\tdest-port\t\t%d\n\ttype\t\t\t%d\n\toffset_from_aperture\t0x%llx\n\tsize\t\t\t0x%llx\n\n",
        str, 
        (int) ltcp_msg->src_ip,
        (int) ltcp_msg->dest_ip,
        (int) ltcp_msg->src_port,
        (int) ltcp_msg->dest_port,
        (int) ltcp_msg->type,
        ltcp_msg->offset_from_aperture,
        ltcp_msg->size);
}

void sprint_ltcp_header(char * buff, struct ltcp_queue_msg * ltcp_msg){
      sprintf(buff,"header:\n\tsrc-ip\t\t\t%d\n\tdest-ip\t\t\t%d\n\tsrc-port\t\t%d\n\tdest-port\t\t%d\n\ttype\t\t\t%d\n\toffset_from_aperture\t0x%llx\n\n", 
        (int) ltcp_msg->src_ip,
        (int) ltcp_msg->dest_ip,
        (int) ltcp_msg->src_port,
        (int) ltcp_msg->dest_port,
        (int) ltcp_msg->type,
        ltcp_msg->offset_from_aperture,
        ltcp_msg->size);
}



/* 
 * Callback for segment creation.
 */
int create_segment_callback(void *arg,
                            sci_l_segment_handle_t local_segment_handle,
                            __u32 reason,
                            __u32 source_node,
                            __u32 local_adapter_number)
{
    /* to be used if you want to know whats happening, otherwise just ignore */
   return 0;
}



/* 
 * Callback for client segment connect 
 * TODO: move to common file.
 */
int client_segment_callback(void *arg,
                            sci_r_segment_handle_t remote_segment_handle,
                            __u32 reason,
                            __u32 status)
{
    int *err = (int*)arg;
    if (status != 0) {
        printk("%s: client_segment_callback unsuccessful: reason 0x%x\n", PROGRAM_NAME, reason);
        *err = 1;
    }

    /* 
     * Exit semaphore to signal segment connected. 
     */
    up(&client_semaphore);

    return 0;
}





/* 
 * Should be triggered by interrupt.
 */
int rdma_recv(void) {
    struct sk_buff *skb;
    struct ltcp_queue_msg *ltcp_msg;
    struct iphdr *iph;
    struct ltcp_ring_queue * rq = ltcp_connection.recv_queue;


    spin_lock(&recv_queue_lock);


    /* 
     * Loop over the recv queue. 
     */
    while(rq->start != rq->end){    

        /* 
        * If it reaches end of header allocation, start from 0. 
        */ 
        if(rq->start+1 == LTCP_MSG_QUEUE_SIZE){
            rq->start = 0; 
            if(rq->end == rq->start) //just check that end also is not 0.
                goto out;   
        }

        ltcp_msg = &(rq->queue[rq->start++]);
       	#ifdef LTCP_DEBUG
        print_ltcp_header("header read", ltcp_msg);
		#endif

     
        skb = alloc_skb(LTCP_MAX_HEADER_SIZE, GFP_ATOMIC);
        


        /* 
         * Reset mac header to point to skb->data
         */
        skb_reset_mac_header(skb);
        
        /* 
         * Assuming this increases headroom by moving data and tail (?). Assuming this only works when data = tail meaning there is no data yet.
         * This will make the eth header a part of the headroom.
         */
        skb_reserve(skb, ETH_HLEN);
        
        /* 
         * Reset the ip_hdr to curret data pointer.
         */
        skb_reset_network_header(skb);
    
        /* 
         * TODO: fill in other info needed.
         * Here we could potentially have filled in the information in eth header as in disip driver and pass it up the stack the regular way through the linux ip layer.
         */
        skb_reserve(skb, sizeof(struct iphdr));
        iph=ip_hdr(skb);
        iph->daddr = ltcp_msg->dest_ip;
        iph->saddr = ltcp_msg->src_ip;

        /* 
         * Reset the transport header to the current skb->data.
         */
        skb_reset_transport_header(skb);

        /* 
         * Reserve space for transport header.
         */
        skb_reserve(skb, sizeof(struct ltcp_queue_msg *));
        memcpy(skb_transport_header(skb), &ltcp_msg, sizeof(struct ltcp_queue_msg *));

        /* 
         * Pass the skb to the local tcp skb receive function. 
         */
        if(local_tcp_rcv(skb)) 
            goto err_out;
    }

out:
    spin_unlock(&recv_queue_lock);
    return 0;

err_out:
    spin_unlock(&recv_queue_lock);
    return -1;
}



/* 
 * Interrupt callback function that is executed when interrupt is triggered 
 * This can be triggered remotely by connecting to the remote interrupt with sci_connect_interrupt_flag and triggered with the sci_trigger_interrupt.
 */
int interrupt_callback(__u32 local_adapter_number,
                       void *arg,
                       __u32 interrupt_number)
{
    #ifdef LTCP_DEBUG
    printk("\nInterrupt received. Calling rdma_recv() to recv packet.\n");
    #endif
    return rdma_recv();
}




/* 
 * This function should take a message as a number of dma_handles and write meta data to remote node.
 */
int rdma_xmit(struct inet_sock *inet_sk, __u32 type, __u64 start, __u64 nr_pages, __u16 offset_first_page, __u64 size){
    
    osif_iova_handle_t dma_handle; 
    scierror_t scierr;
    struct ltcp_queue_msg* ltcp_msg;
    __u64 offset_from_aperture, data_bus_addr;
    int callback_error; 
	int err, locked;

    offset_from_aperture = 0;

    if(type == REGULAR){
    #ifdef MAP_USER_PAGES_SEND

    /* 
     * Allocate space for page pointers. 
     * Max needed is size/PAGE_SIZE + two extra in case of mismatch.
     */
    struct page ** page_list = (struct page **) vmalloc( (size/PAGE_SIZE + 2) * sizeof(struct page *)); //[len/PAGE_SIZE + 2];
    
    /* 
    * Map the message to the IOMMU ring buffer.  
    * If IOMMU range not available it should probably sleep and others should wait for this got get its allocation? (Should think about this inorder to prevent starvation)
    */ 


        /*     
        * An array to hold the pointer to iova_addr structure handles. Will be the same size as the number of messages we are sending.
        */

        
        /* Change to pin_user_pages? (nope, not available in 5.4 LINUX)
        *        https://lwn.net/Articles/807108/ : 
        *              "The question of how a developer should choose between get_user_pages() and pin_user_pages() is somewhat addressed in the documentation update found in this patch. In short, if pages are being pinned for access to the data contained within those pages, pin_user_pages() should be used. 
        *               For cases where the intent is to manipulate the page structures corresponding to the pages rather than the data within them, get_user_pages() is the correct interface."
        * TODO: Should check the get_user_pages_fast which should be used for performance critical applications. 
        *
        * Pages should also be pinned to memory by this function, according to docs. 
        */

        /* 
         * Should fetch the user space pages and add kernel struct page * to the page_list array.  
         */
        locked = 1;
        down_read(&current->mm->mmap_sem);
        err = get_user_pages_locked(
            start-offset_first_page,  	
            nr_pages,
            0, 
            page_list, 	
            &locked); 

        if(locked){
            up_read(&current->mm->mmap_sem);
        }else{
            printk(KERN_ALERT "critical section was lost!!! \n");
        }

        if(err != nr_pages){
            printk(KERN_ALERT "!!!!! Couldn't find page . !!!!\n");
            // TODO: This should cause an error as we need this to be loss less we cannot accept pages not being mapped for send.
            return -1; 
        }

        if(page_list){

            if(osif_iommu_map_user_pages(aperture_range, page_list, nr_pages, &dma_handle) != OSIF_ERR_OK){
                printk("osif_iommu_map_user_pages failed.\n");
                return -1;
            }
            offset_from_aperture = osif_iommu_handle_address(dma_handle)-osif_iommu_subrange_start(aperture_range) + offset_first_page;   

        } else {

            printk("Failed to get user space pages\n");
            return -1;
        }

    #else
        /* 
        * Seems unlogical to use page list if there is write to remote mem? Maybe just pass NULL 
        * We must setup an physical address range and copy into or use PIO. PIO must be done in the address space of the process. 
        */
        // TODO: implement wq
        while(space_left(ltcp_connection.send_queue) < size){
            msleep(1);
        }
        
        offset_from_aperture = ltcp_connection.send_queue->phys_start;   
        
        /* 
            * wrap and split copy if necessary
            */
        if(offset_from_aperture + size >= APERTURE_SIZE_QUEUE){
            ltcp_connection.send_queue->phys_start = (offset_from_aperture + size) - APERTURE_SIZE_QUEUE;
            /* 
                * Copy to end
                */
            copy_from_user((void *)(physical_send_aperture+offset_from_aperture), start, APERTURE_SIZE_QUEUE - offset_from_aperture);        
            /* 
                * Copy rest from start
                */
            copy_from_user((void *)physical_send_aperture, start+(APERTURE_SIZE_QUEUE - offset_from_aperture), size - (APERTURE_SIZE_QUEUE - offset_from_aperture));        
        }else {
            
            ltcp_connection.send_queue->phys_start = offset_from_aperture + size;
            copy_from_user((void *)(physical_send_aperture+offset_from_aperture), start, size);        
        }

    #endif
    }
    
    
    /* 
     * Make sure that it will fill info before any other process adds a new message. 
     * This is so that the receiver knows that there isn't any messages up to ltcp_connection->end that is not yet filled.
     * Maybe replace with critical section? Such that is won't be interrupted?
     */
    spin_lock(&send_queue_lock);

    /*
     * Get free message idx from metadata ring buffer.   
     */
    ltcp_msg = get_free_metamessage(ltcp_connection.send_queue);

    if(!ltcp_msg){
        printk("Could not get free message header in send queue. Package is dropped as we have not implemented fix for this yet.\n");
        spin_unlock(&send_queue_lock);
        //return -1;
        /* 
         * TODO: Should add process to sleep queue?? (important that package is not lost...) 
         */ 
    }else{
        
        /* 
         * Fill the tcp message meta data.
         */
        ltcp_msg->src_ip = inet_sk->inet_saddr; 
        ltcp_msg->dest_ip = inet_sk->inet_daddr; 
        ltcp_msg->src_port = inet_sk->inet_sport; 
		ltcp_msg->dest_port = inet_sk->inet_dport;
		ltcp_msg->type = type;
        ltcp_msg->size = size;


        /* 
         * If we allow for mapping on the recv side there is a difference in whether  or not we are going to use the IOMMU aperture or the physical address start. 
         */
        // #ifdef MAP_USER_PAGES_RECV
        ltcp_msg->offset_from_aperture = offset_from_aperture;
        // #else
        // ltcp_msg->offset_from_aperture = local_send_start;
        // #endif

        /* 
         * NOTE: important that end is only updated after it has filled the message. Because the receiver should not read it before it is filled, and will only read until "end".
         */
        #ifdef LTCP_DEBUG
        print_ltcp_header("header send", ltcp_msg);
		#endif
    }

    #ifndef PIO
    /* 
     * If we don't map and read from remote side we must write into remote physical contigous address. 
     */
    #ifndef MAP_USER_PAGES_RECV
    if(type==REGULAR){



        /* 
        * Before we can call transfer we enter a semaphore. 
        * When the callback is called it will "release" the semaphore again.
        */
        down(&client_semaphore); 

        /* 
         * Where the data we should read is depends on if we are mapping or copying on the send side.
         */
        // #ifdef MAP_USER_PAGES_SEND
        // data_bus_addr = osif_iommu_handle_address(dma_handle) + offset_first_page;
        // #else
        data_bus_addr = osif_iommu_handle_address(dma_handle_aperture) + offset_from_aperture;
        // data_bus_addr = physical_send_aperture+offset_from_aperture;
        // #endif

        #ifdef LTCP_DEBUG
        printk("\ndis_start_dma_transfer:\n\tlocal-busaddr\t\t0x%llx\n\tdata-size\t\t0x%lx\n\toffset-remote-aperture\t0x%llx\n\n", 
            data_bus_addr,
            size,
            ltcp_msg->offset_from_aperture);
        #endif  

        /* 
        * Fetch the pages from the remote node and add it to the skb. 
        *TODO: change to using ltcp_msg->offset_first_page after testing. 
        */
        scierr = dis_start_dma_transfer(SUBUSERID_GENERIC,
            NULL, 
            data_bus_addr, 
            size, 
            offset_from_aperture,  
            rsh_aperture_recv,
            client_dma_callback,
            &callback_error,
            NO_ARG,
            0);

        
        if (!sci_error_handler(scierr, "dis_start_dma_transfer")) {
            spin_unlock(&send_queue_lock);
            return -1;
        }

        /* 
        * Wait for callback before proceeding 
        */
        if (down_timeout(&client_semaphore, SEMAPHORE_TIMEOUT) != 0) {
            printk("%s: semaphore timeout!\n", PROGRAM_NAME);
            spin_unlock(&send_queue_lock);
            return -1;
        }

        if (callback_error) {
            printk("error on callback: %lx\n", callback_error);
            spin_unlock(&send_queue_lock);
            return -1;
        }

        up(&client_semaphore);
    }
    #endif
    #endif


    if(ltcp_connection.send_queue->end+1 == LTCP_MSG_QUEUE_SIZE){
        ltcp_connection.send_queue->end = 0; 
    }

    /*
     * Signal that there is one more buffer in the send queue.
     */ 
    ltcp_connection.send_queue->end++;
    
    /*
     * Trigger interrupt on the remote node to notify about new data. 
     */
    #ifdef LTCP_DEBUG
    printk("%s: trigger interrupt number %d on node %d\n", PROGRAM_NAME, ltcp_connection.send_queue->int_no, remote_node_id);    
    #endif

    scierr = sci_trigger_interrupt(rih);
    if (!sci_error_handler(scierr, "sci_trigger_interrupt")) {
        spin_unlock(&send_queue_lock);
        return -1;
    }
    spin_unlock(&send_queue_lock);

    return 0;
}
EXPORT_SYMBOL(rdma_xmit);



/*
 * https://www.kernel.org/doc/Documentation/DMA-API-HOWTO.txt
 * 
 * This function should utilize RDMA and IOMMU to write data to remote node. 
 */
int ltcp_direct_copy(struct inet_sock *sk, struct msghdr *msg, size_t len, __u32 type){

	/*
	 * First I would need to pin the memory in Main memory and get the location of the pages.
	 */
	__u64 offset_first_page, start, i, pages_to_send, left_to_send; 
    int nr_pages;
    struct iovec iov;


	/* 
	 * For all the different messages in the message array. 
	 */
    if(type == REGULAR){
        
        
        iov = iov_iter_iovec(&msg->msg_iter);

        #ifdef LTCP_DEBUG
        printk("iov_len: %d\n", (int)iov.iov_len);
        printk("len: %d\n", (int)len);
        #endif

        start = (unsigned long) iov.iov_base;
		offset_first_page = start % PAGE_SIZE;
        nr_pages = calc_nr_pages(start, len);

        
        /* 
        * Just asserting that there isn't a mistake in calculation or that len is wrong compared to the total len of iov_lens
        */
        #ifdef LTCP_DEBUG
        if(nr_pages > (len/PAGE_SIZE + 2)){
            printk(KERN_ALERT "Something is wrong with calculation. Using too many pages \n");
            return -1;
        }
        #endif


        
        /*
        
        Although the <offset>
        and <size> parameters are provided to do partial page mapping, it is
        recommended that you never use these unless you really know what the
        cache width is.

        https://www.kernel.org/doc/Documentation/DMA-API.txt
        
        If this is an issue I could possibly copy the first and last page if it is not a full page into a new page and allocate the entire page.
        NOTE: This i done underneath, but maybe I don't need to. 
        */


        /*
         * TODO: Write about the security issue here, and how we don't care to solve this. We make bytes in the first and last page available to the remote side. 
         * This is not ideal, but I feel it is important to note that this is computer connected in a local network and probably controlled by the same entity. 
         * The TCP communication likely goes between two programs by the same developer(s), so it is likely that we can trust the program on the other end.  
         * There is however a consern which should be handled if situation is different. 
         * We could solve this by allocating two new pages for the first and the last page if they are not page-aligned. 
         * We can then copy the two pages and zero out the part not used, and instead map the two new pages. 
         * 
         */        

        /*
        * Before this the pages should be mapped into iommu.
        * We must now share this with the remote node.
        * This is done through shared memory ringbuffer containing the metadata needed to send the packet. 
        */
        left_to_send = len;
        for(i = 0; i < nr_pages; i+=MAX_PAGES_SEND){
            pages_to_send = (MAX_PAGES_SEND < nr_pages-i ? MAX_PAGES_SEND : nr_pages-i);

            // /* 
            //  * Only offset first round.
            //  */
            // if(i) offset_first_page = 0;
            
            /* 
             * if last round
             */
            if(pages_to_send == (nr_pages-i)){
                if(rdma_xmit(sk, type, start+(i*PAGE_SIZE), pages_to_send, (__u16) offset_first_page, (__u64) left_to_send)) return -1;
                return 0;
            }else{
                if(rdma_xmit(sk, type, start+(i*PAGE_SIZE), pages_to_send, (__u16) offset_first_page, (__u64) pages_to_send * PAGE_SIZE)) return -1;
                left_to_send -= pages_to_send * PAGE_SIZE;
            }
        }

        return 0;

    /*TODO: must call put_page to "free" the page  and dma_unmap_page(). Do we need to flush or is this not an issue? */
    
    }
    /* 
     * If type is not regular .
     */

    return rdma_xmit(sk, type, 0, 0, 0, 0);
}

/* 
 * Create segment for connection communication.
 */
int setup_recv_meta_queue(void){

    scierror_t scierr;
  
    /*
     * Create a local segment of contingous memory.
     */
    scierr = sci_create_segment(sci_binding,
                                MODULE_ID,
                                SHARED_SEGMENT_ID_META_QUEUE,
                                NO_FLAGS,
                                SEGMENT_SIZE,
                                create_segment_callback,
                                NO_ARG,
                                &lsh_meta_queue);
    if (!sci_error_handler(scierr, "sci_create_segment")) {
        printk("Failed to create segment.\n");
        return -1;
    }

    /*
     * Export segment to make available for connect from other nodes.
     */
    scierr = sci_export_segment(lsh_meta_queue,
                                local_adapter_number,
                                NO_FLAGS);
    if (!sci_error_handler(scierr, "sci_export_segment")) {
        return -1;
    }

    /* 
     * Get virtual address of the shared memory segment and fill the send_queue.   
     */
    ltcp_connection.recv_queue = (struct ltcp_ring_queue *) sci_local_kernel_virtual_address(lsh_meta_queue);

    /* 
     * Set all to 0; (TODO: memset?)
     */
    ltcp_connection.recv_queue->start = 0;
    ltcp_connection.recv_queue->end = 0;
    ltcp_connection.recv_queue->phys_start = 0;
    ltcp_connection.recv_queue->phys_end = 0;
    


    /* 
     * Create interrupt by allocating interrupt flag. 
     */
    scierr = sci_allocate_interrupt_flag(sci_binding,
                                         local_adapter_number,
                                         PRIORITY,
                                         NO_FLAGS,
                                         interrupt_callback,
                                         NO_ARG,  
                                         &lih);
    if (!sci_error_handler(scierr, "sci_allocate_interrupt_flag")) {
        return -1;
    }

    /* 
     * Get interrupt number from handle.
     */
    ltcp_connection.recv_queue->int_no = sci_interrupt_number(lih);

    return 0;
}





/*
 * Connect to the remote shared communication segment.
 */
int setup_send_meta_queue(void){

    sci_map_handle_t map_handle;
    scierror_t scierr;
    int callback_error = 0; 

    /* 
     * Enter semaphore and try to connect to segment 
     */
    down(&client_semaphore);

    /*
     * Get the remote segment handler.
     */
    scierr = sci_connect_segment(sci_binding,
                                 remote_node_id,
                                 local_adapter_number,
                                 MODULE_ID,
                                 SHARED_SEGMENT_ID_META_QUEUE,
                                 NO_FLAGS,
                                 client_segment_callback,
                                 &callback_error,
                                 &rsh_meta_queue);
    
    if (!sci_error_handler(scierr, "sci_connect_segment")) {
        return -1;
    }



    /* 
     * Wait for callback before proceeding 
     */
    if (down_timeout(&client_semaphore, SEMAPHORE_TIMEOUT) != 0) {
        printk("%s: semaphore timeout!\n", PROGRAM_NAME);
        return -1;
    }
    
    /* 
     * Check if callback error.
     */
    if (callback_error) {
        return -1;
    }

    up(&client_semaphore);

    /* 
     * The segment is used as the send meta queue. 
     */
    scierr = sci_map_segment(rsh_meta_queue, NO_FLAGS, 0, SEGMENT_SIZE,  &map_handle);
    if (!sci_error_handler(scierr, "sci_map_segment")) {
        return -1;
    }

    /* 
     * This should get the send queue, which is the receive queue of the remote node. 
     */
    ltcp_connection.send_queue = (struct ltcp_ring_queue * ) sci_kernel_virtual_address_of_mapping(map_handle); //TODO: check that is works with remote_segment_handle.


    /* 
     * Connect to the remote interrupt.
     */
    scierr = sci_connect_interrupt_flag(sci_binding,
                                        remote_node_id,
                                        local_adapter_number,
                                        ltcp_connection.send_queue->int_no,
                                        NO_FLAGS,
                                        &rih);
    if (!sci_error_handler(scierr, "sci_connect_interrupt_flag")) {
        return -1;
    }

    return 0;
}



/* 
 * Create segment for connection communication.
 */
int setup_recv_aperture_queue(void){

    scierror_t scierr;
  
    /*
     * Create a local segment.
     */
    scierr = sci_create_segment(sci_binding,
                                MODULE_ID,
                                SHARED_SEGMENT_ID_APERTURE,
                                DONT_ALLOCATE,
                                APERTURE_SIZE_QUEUE,
                                create_segment_callback,
                                NO_ARG,
                                &lsh_aperture);
    
    if (!sci_error_handler(scierr, "sci_create_segment")) {
        printk("Failed to create segment.\n");
        return -1;
    }


    /* 
     * Create and aperture attached to the local segment handle. 
     * This should now be available for connect from the remote node. 
     */ 
    scierr = sci_alloc_aperture(lsh_aperture, 
                                local_adapter_number, 
                                NO_FLAGS, 
                                APERTURE_SIZE_QUEUE, 
                                &aperture_range);

    if (!sci_error_handler(scierr, "sci_alloc_aperture")) {
        printk("Failed to create segment.\n");
        return -1;
    }

    #ifndef MAP_USER_PAGES_RECV
    scierr = sci_create_segment(sci_binding,
                                MODULE_ID,
                                SHARED_SEGMENT_ID_APERTURE_RECV,
                                0,
                                APERTURE_SIZE_QUEUE,
                                create_segment_callback,
                                NO_ARG,
                                &lsh_aperture_recv);
    if (!sci_error_handler(scierr, "sci_create_segment")) {
        printk("Failed to create segment.\n");
        return -1;
    }


    /*
     * Export segment to make available for connect from other nodes.
     */
    scierr = sci_export_segment(lsh_aperture_recv,
                                local_adapter_number,
                                NO_FLAGS);
    if (!sci_error_handler(scierr, "sci_export_segment")) {
        return -1;
    }
    physical_recv_aperture = sci_local_kernel_virtual_address(lsh_aperture_recv);
    // physical_recv_aperture = (uint64_t) (vkptr_t) __get_free_pages(GFP_ATOMIC, get_order(APERTURE_SIZE_QUEUE));


    #endif

    return 0;
}



/*
 * Connect to the remote shared communication segment.
 */
int setup_send_aperture_queue(void){
    scierror_t scierr;
    int callback_error = 0; 
    sci_map_handle_t map_handle;

    #ifdef MAP_USER_PAGES_RECV
    
    /* 
     * Enter semaphore and try to connect to segment 
     */
    down(&client_semaphore);

    /*
     * Get the remote segment handler.
     */
    scierr = sci_connect_segment(sci_binding,
                                remote_node_id,
                                local_adapter_number,
                                MODULE_ID,
                                SHARED_SEGMENT_ID_APERTURE,
                                NO_FLAGS,
                                client_segment_callback,
                                &callback_error,
                                &rsh_aperture);
    
    
    if (!sci_error_handler(scierr, "sci_connect_segment")) {
        return -1;
    }
   

    /* 
     * Wait for callback before proceeding 
     */
    if (down_timeout(&client_semaphore, SEMAPHORE_TIMEOUT) != 0) {
        printk("%s: semaphore timeout!\n", PROGRAM_NAME);
        return -1;
    }
 
    /* 
     * Check for callback error.
     */
    if (callback_error) {
        return -1;
    }
    up(&client_semaphore);

    #endif






    #ifndef MAP_USER_PAGES_RECV
    /* 
     * Enter semaphore and try to connect to segment 
     */
    down(&client_semaphore);

    /*
     * Get the remote segment handler.
     */
    scierr = sci_connect_segment(sci_binding,
                                remote_node_id,
                                local_adapter_number,
                                MODULE_ID,
                                SHARED_SEGMENT_ID_APERTURE_RECV,
                                NO_FLAGS,
                                client_segment_callback,
                                &callback_error,
                                &rsh_aperture_recv);
    
    
    if (!sci_error_handler(scierr, "sci_connect_segment")) {
        return -1;
    }
    
    
    /* 
     * Wait for callback before proceeding 
     */
    if (down_timeout(&client_semaphore, SEMAPHORE_TIMEOUT) != 0) {
        printk("%s: semaphore timeout!\n", PROGRAM_NAME);
        return -1;
    }
    /*  
     * Check for callback error.
     */
    if (callback_error) {
        return -1;
    }

    up(&client_semaphore);


    #ifdef PIO
    /* Map remote segment */
    scierr = sci_map_segment(rsh_aperture_recv, 
                             NO_FLAGS,  
                             0, 
                             APERTURE_SIZE_QUEUE,
                             &map_handle);
    if (scierr != ESCI_OK) {
        printk("sci_map_segment failed - error 0x%x\n",
                scierr);
    } 

    physical_send_aperture = sci_kernel_virtual_address_of_mapping(map_handle);
    #else
    scierr = sci_create_segment(sci_binding,
                            MODULE_ID,
                            SHARED_SEGMENT_ID_APERTURE_SEND,
                            0,
                            APERTURE_SIZE_QUEUE,
                            create_segment_callback,
                            NO_ARG,
                            &lsh_aperture_recv_segment);
    if (!sci_error_handler(scierr, "sci_create_segment")) {
        printk("Failed to create segment.\n");
        return -1;
    }
    // scierr = sci_export_segment(lsh_aperture_recv_segment,
    //                             local_adapter_number,
    //                             NO_FLAGS);
    // if (!sci_error_handler(scierr, "sci_export_segment")) {
    //     return -1;
    // }

    /* 
     * Create and aperture attached to the local segment handle. 
     * This should now be available for connect from the remote node. 
     */ 
    scierr = sci_alloc_aperture(lsh_aperture_recv_segment, 
                                local_adapter_number, 
                                NO_FLAGS, 
                                APERTURE_SIZE_QUEUE, 
                                &aperture_range_recv);

    if (!sci_error_handler(scierr, "sci_alloc_aperture")) {
        printk("Failed to create segment.\n");
        return -1;
    }

    physical_send_aperture = sci_local_kernel_virtual_address(lsh_aperture_recv_segment);
    if(osif_iommu_map(aperture_range_recv, physical_send_aperture, APERTURE_SIZE_QUEUE, &dma_handle_aperture, NO_FLAGS)){
        free_pages((unsigned long) physical_recv_aperture, get_order(APERTURE_SIZE_QUEUE));
        osif_warn("%s: osif_iommu_map failed", __FUNCTION__);
        return -1;
    }
    // sci_local_io_addresses(lsh_aperture_recv_segment, local_adapter_number, &ioaddr_list);
    // // physical_send_aperture = (uint64_t) (vkptr_t) __get_free_pages(GFP_ATOMIC, get_order(APERTURE_SIZE_QUEUE)); 
    // physical_send_aperture = kmalloc(APERTURE_SIZE_QUEUE, GFP_ATOMIC); 
    if(!physical_send_aperture){
        printk("Failed to allocate physical send aperture.\n");
        return -1;
    }
    #endif

    #endif

    

    // #ifndef MAP_USER_PAGES_SEND
     
    // #endif

    return 0;
}



/*
 * TODO: Should maybe have a thread for this communication that checks if there is any new messages or can I use interrupt?
 */
int daemon_setup(void){

    uint64_t rem_iova;
    /* 
     * Check if IOMMU present. 
     */
    // if( osif_iommu_present() != OSIF_ERR_OK ){
    //     printk("No IOMMU present. Running without IOMMU not yet supported. \n");
    //     return -1;
    // 


    /*
     * Setup the shared memory segment for receive queue and share it with remote node.
     */
    if(setup_recv_meta_queue() < 0){
        return -1;
    }
    printk("recv_meta_virt:\t\t\t%llx\nrecv_meta_phys:\t\t\t%llx\nrecv_int_nr:\t\t\t%d\n", (unsigned long long) ltcp_connection.recv_queue, (unsigned long long) sci_local_segment_phys_address(lsh_meta_queue), ltcp_connection.recv_queue->int_no);
    
    /* 
     * Create an aperture (an IOVA range allocation) used to map 
     */
    if(setup_recv_aperture_queue() < 0){
        return -1;
    }
    printk("local_aperture_local_iova:\t%llx\n", (unsigned long long) sci_local_io_addr(lsh_aperture, local_adapter_number));


   /* 
    * This is just a temporary solution to give the remote node time to start.  
    */
    msleep(20000);

    /* 
     * Setup the share memory segment structure for send queue and connect it to remote receive queue. 
     */
    if(setup_send_meta_queue() < 0){
        return -1;
    }
    printk("send_meta_virt:\t\t\t%llx\nsend_meta_phys:\t\t\t%llx\nsend_int_nr:\t\t\t%d\n", (unsigned long long) ltcp_connection.send_queue, (unsigned long long) virt_to_phys(ltcp_connection.send_queue), ltcp_connection.send_queue->int_no);

    /* 
     * Connect to remote aperture. 
     */
    if(setup_send_aperture_queue() < 0){
        return -1;
    }

    return 0;
}



/* Main function */
int daemon_setup_wrapper(void)
{
    /* Initialize with module id */
    if (!sci_initialize(MODULE_ID)) {
        printk(KERN_ERR "%s: %s: Cannot initialize GENIF\n", PROGRAM_NAME, __FUNCTION__);
        return -1;
    }
    
    /* 
     * What did this do again? 
     */
    sci_bind(&sci_binding);

    /*
     * Start the daemon.
     */
    if(daemon_setup()){
        return -1;
    } 

    return 0;
}




/* 
 * Cleanup 
 */
void cleanup(void)
{
    local_tcp_module_exit_function();

    printk("Exiting the ltcp deamon and releasing all resources. \n");
    /* Use sci_unbind to free all resources associated with binding */
    
    sci_error_handler(sci_unbind(&sci_binding), "sci_unbind");
    sci_terminate(MODULE_ID);
}

/* 
 * Initize the module.
 */
int init(void) {
    printk("Initializing the ltcp daemon. \n");
    
    /*  
     * Initizide the the ltcp protocol "module".
     */
    local_tcp_module_init_function();

    /*
     * If the communication will go over RDMA instead of ethernet. (Ethernet is used primarily for testing. )
     */
    if (daemon_setup_wrapper()) {
        cleanup();
        return -1;
    }

    return 0;
}


/* Register init and exit functions */
module_init(init);
module_exit(cleanup);

module_param(remote_node_id, int, 0);

MODULE_AUTHOR("Dolphin ICS");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module for local tcp");
