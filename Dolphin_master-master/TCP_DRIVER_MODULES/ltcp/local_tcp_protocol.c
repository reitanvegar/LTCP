

/* 
 * Header file for this protocol module. 
 */
#include "local_tcp_protocol.h"
#include "../rdma/rdma_daemon.h"

/* 
 * Include helper functions and port funcions. 
 */
#include "helpers.c"
#include "port_functions.c"

#ifdef LINK_IS_ETHERNET
#include "ethernet.c"
#endif



/* 
 * Callback for the dma transfer.
 */ 
int client_dma_callback(void *arg, dis_dma_status_t status) {
    int *err = (int*)arg;

    if (status != 0) {
        printk("%s: client_dma_callback unsuccessful: status 0x%x\n",
               PROGRAM_NAME, status);
        *err = 1;
    }

    /* Exit semaphore to signal connect_segment success */
    up(&client_semaphore);

    return 0;
}

/* 
 * Helper function to print error messages 
 */
int sci_error_handler(scierror_t scierr, char *function_name)
{
    if (scierr != ESCI_OK) {
        printk(KERN_ALERT "%s: %s failed: error 0x%x\n",
               PROGRAM_NAME, function_name, scierr);
        return 0;
    }
    return 1;
}


/*
 * Calculate the number of pages for a given start and size. 
 */
__u64 calc_nr_pages(__u64 start, __u64 len){
	__u64 nr_pages, offset_first_page, offset_last_page;
	#ifdef LTCP_DEBUG
    printk("Calculating pages needed from 0x%llx to 0x%llx\n", start, start + len);
	#endif

	nr_pages = 1; 
	offset_last_page = (start+len) % PAGE_SIZE;
	offset_first_page = start % PAGE_SIZE;
	if((offset_first_page + len) >  PAGE_SIZE){
		nr_pages += (len - (PAGE_SIZE - offset_first_page)) / PAGE_SIZE; 
		if(offset_last_page)
			nr_pages++;
	}
	#ifdef LTCP_DEBUG
    printk("nr_pages: (%lld)\n", nr_pages);
	#endif

	return nr_pages;
}


/*
 *  
 */
int local_tcp_sendmsg_helper(struct ltcp_sock *sk, struct msghdr *msg, size_t len, __u32 type){
	struct inet_sock * inet;
	inet = (struct inet_sock *) sk; 

	/* 
	 * Handle either as ethernet or RDMA 
	 */
	#ifndef LINK_IS_ETHERNET
		return ltcp_direct_copy(inet, msg, len, type);
	#else
		return ethernet_send(inet, msg, len, type);
	#endif
}




int loop_and_close_sockets(struct port_sk_arr * arr, int lport, struct timespec ts){
	struct port_sk_arr * conn_arr;
	struct port_sk* current_psk;
	struct port_sk* psk;
	int err; 
	conn_arr = &(arr[lport]); 
	current_psk = conn_arr->head; 

	while(current_psk != NULL){
		psk = NULL;
		if(current_psk->sk->last_update.tv_sec + KEEP_ALIVE_TIME < ts.tv_sec){

			/* 
			* Tell remote computer we are closing the connection.
			*/
			err = local_tcp_sendmsg_helper(current_psk->sk, NULL, 0, CLOSE);
			if(err){
				printk("Failed to send CLOSE message\n");
				return err;
			}

			psk = remove_port_sk(conns, inet_sk((struct sock *)current_psk->sk)->inet_sport, inet_sk((struct sock *)current_psk->sk)->inet_dport, inet_sk((struct sock *)current_psk->sk)->inet_daddr);
	
		}
		current_psk = current_psk->next;
		if(psk)
			kfree(psk);
	}


	return 0;
}





/* 
 * The protocol will be call with the data to send. 
 */
int local_tcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len){

	return local_tcp_sendmsg_helper(ltcp_sk(sk), msg, len, REGULAR);
}
EXPORT_SYMBOL(local_tcp_sendmsg);





/*
 *
 * Could also consider to add the scatterlist dma_address as dma_map_sg would do. Sharing this with the remote computer would enable
 * the possibily of mapping it into dma in non contingous order. Which could be an advantage if there is no region large enough for the dma mapping.
 */
int osif_iommu_map_user_pages(osif_iova_range_t *range, struct page ** page_list, int page_list_len, osif_iova_handle_t *handle){	

    uint64_t len;
    osif_memblock_t *block;
    osif_mempool_t *pool = NULL;
    int rc, i;
    uint64_t iova, paddr;
    size_t alignment;
	struct page * current_page;
    rc = 0; 

    if (!range) {
        printk("IOMMU range is NULL \n");
        return -1;
    }
    
    /* 
     * We assume each scatterlist entry is a full page. 
     */
    len = page_list_len*OSIF_PAGE_SIZE;
    if(len <= 0){
        printk("Length has to be greater than 0\n");
        return -1;
    }


    /*
     * We get a block of data from the range allocator.
     * TODO: Need to add a thread that removes allocation after x seconds if not used, in order to clean up. 
     *
     * TODO: TODO: Here I can make the process sleep and wait for allocation to be ready. Basically try again at a later time. 
     * This is in order for it to be lossless. I can then wake them up when aperture area is released by other process. Basically notifying it of new area free for allocation. 
     */
	#ifdef LTCP_DEBUG
    printk("Allocating with osif_allocator_alloc. Total size 0x%llx, Alignment 0x%lx\n", len, OSIF_PAGE_SIZE);
	#endif


    block = osif_allocator_alloc(&range->allocator, len, OSIF_PAGE_SIZE, 0, &pool);

    if(!block) {
        osif_warn("No room in IOMMU range for map size=0x%llx with 0x%lx alignment", len, OSIF_PAGE_SIZE);
        osif_allocator_dump(&range->allocator);
        return OSIF_ERR_NOSPC;
    }

    /* 
     * Instead of mapping the pages by assuming the page to be physically contingous we map page by page.
     * 
     */
    for(i=0; i < page_list_len; i++){
		current_page = page_list[i];

        /* 
         * Here we map the page into iommu memory. 
         * The arguments: 
         *     domain, iova address, physical address, size to map, protocol (read and write).
         */
        iova = block->addr + OSIF_PAGE_SIZE * i;
		paddr = page_to_phys(current_page); 
        alignment = OSIF_PAGE_SIZE;

		#ifdef LTCP_DEBUG
				// printk("\nMapping page:\n\tiova\t\t\t0x%llx\n\tpaddr\t\t\t%llx\n\talignment\t\t%zx\n\n", iova, paddr, alignment);
		#endif

        rc = iommu_map(range->domain->domain, iova, paddr, alignment, IOMMU_READ | IOMMU_WRITE);
     
        /*
         * If there is an error in map we must unmap. 
         * TODO: This is done in the dolphin implemention, but in Linux 5.4 this seems to be handled in the iommu_map function. 
         *       Therefore not sure if it should be removed or not. 
         */
        if(rc) {
            printk(KERN_ALERT "iommu_map failed!!! Unmapping... ");
            /*
            * Incase the mapping fails we loop over and unmap the pages already mapped. 
            */
            for(;i>0;i--){
                iommu_unmap(range->domain->domain, block->addr + OSIF_PAGE_SIZE * (i-1), OSIF_PAGE_SIZE);
            }
            break;
        }

    }


    if (rc) {
        osif_warn("iommu_map failed (%d): iova=%llx, len=%llx", rc, block->addr, len);
        osif_allocator_free(&range->allocator, block);
        rc = OSIF_ERR_NOMEM;
        *handle = NULL;
    } else {
        rc = OSIF_ERR_OK;
        *handle = block;
    }
    return rc;
}


/*
 * This should read ... TODO: addr_len? 
 */
int local_tcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len, int nonblock, int flags, int *addr_len){
	scierror_t scierr;
	long timeo;
	struct sk_buff *skb;
    osif_iova_handle_t dma_handle; 
    struct ltcp_queue_msg *ltcp_msg;
	__u64 data_bus_addr, nr_pages, offset_first_page, start;
    struct iovec iov;
	struct page ** pages = (struct page **) vmalloc((len/PAGE_SIZE + 2) * sizeof(struct page *)); //[len/PAGE_SIZE + 2];
  	int callback_error; 
	int err, locked;

	lock_sock(sk);

	timeo = sock_rcvtimeo(sk, nonblock);

	do {
		/* 
		* Read the first element in the queue.
		*/
		skb = skb_peek(&sk->sk_receive_queue);


		if(!skb){
			/* 
			 * Note that the timeout will always be set to 0 if timeout has happened. 
			 * https://elixir.bootlin.com/linux/v5.4/source/kernel/time/timer.c#L1904*/
			sk_wait_data(sk, &timeo, NULL);
			
			/*
			 * Timeout.
			 */
			if(!timeo){
				len = -EAGAIN;
				break;
			}

			/*
			 * Try again
			 */
			continue;
		}

		#ifdef LTCP_DEBUG
		printk("Socket received socket buffer.\n");
		#endif

		/*
		osif_iommu_map_user_pages * Fetch the ltcp_msh header. TODO: Maybe just use ltcp_msg = (struct ltcp_queue_msg *) skb_transport_header;
		 */
		memcpy(&ltcp_msg, skb_transport_header(skb), sizeof(struct ltcp_queue_msg *));

		/*
		 * Calculate the remote data size. 
		 */
		#ifdef LTCP_DEBUG
			printk("iov.iov_len: %ld\n" , iov.iov_len);
			printk("remote_size: %lld\n" , ltcp_msg->size);
			printk("len: %ld\n" , len);
		#endif

		/* 
		 * So if packet is larger than len we must make sure that we do not get a segfault.
		 * TODO: should make sure packet is not discarded if not fully read.  
		 */
		if(ltcp_msg->size < len)
			len = ltcp_msg->size;	
		
			   	
		iov = iov_iter_iovec(&msg->msg_iter);
        start = (unsigned long) iov.iov_base;
		offset_first_page = start % PAGE_SIZE;


		/* 
		 * Calculate pages needed in local buffer.
		 */
		nr_pages = calc_nr_pages(start, len);

        #ifdef MAP_USER_PAGES_RECV

			/* 
			* Get the user pages from the destination buffer. 
            */
			locked = 1;
			down_read(&current->mm->mmap_sem);

			err = get_user_pages_locked(
				start-offset_first_page,  	
				nr_pages,
				0, 
				pages, 	
				&locked); 

			if(locked){
				up_read(&current->mm->mmap_sem);
			}else{
				printk(KERN_ALERT "critical section was lost!!! \n");
			}
			if(err < 0){
				printk("Failed to get the page.\n");
			}
			if(err != nr_pages){
				printk(KERN_ALERT "!!!!! Couldn't find page . !!!!\n");
				// TODO: This should cause an error as we need this to be loss less we cannot accept pages not being mapped for send.
				len = -EFAULT;
				goto out;
			}

			/* 
			 * Map local buffer
			 */
			if(osif_iommu_map_user_pages(aperture_range, pages, nr_pages, &dma_handle) != OSIF_ERR_OK){
				printk("osif_iommu_map_user_pages failed.\n");
				len = -EFAULT;
				goto out;
			}

			data_bus_addr = osif_iommu_handle_address(dma_handle) + offset_first_page;

			/* 
			* Before we can call transfer we enter a semaphore. 
			* When the callback is called it will "release" the semaphore again.
			*/
			down(&client_semaphore); 

			#ifdef LTCP_DEBUG
			printk("\ndis_start_dma_transfer:\n\tlocal-busaddr\t\t0x%llx\n\tdata-size\t\t0x%lx\n\toffset-remote-aperture\t0x%llx\n\n", 
				data_bus_addr,
				len,
				ltcp_msg->offset_from_aperture);
			#endif

			/* 
			* Fetch the pages from the remote node and add it to the skb. 
			*TODO: change to using ltcp_msg->offset_first_page after testing. 
			*/
			scierr = dis_start_dma_transfer(SUBUSERID_GENERIC,
				NULL,
				data_bus_addr, //bus address of the locally mapped segment. 
				len, 
				ltcp_msg->offset_from_aperture,  //remote offset into aperture:  
				rsh_aperture,
				client_dma_callback,
				&callback_error,
				NO_ARG,
				DMA_PULL);
		
			if (!sci_error_handler(scierr, "dis_start_dma_transfer")) {
				len = -EFAULT;
				goto out;
			}

			/* 
			* Wait for callback before proceeding 
			*/
			if (down_timeout(&client_semaphore, SEMAPHORE_TIMEOUT) != 0) {
				printk("%s: semaphore timeout!\n", PROGRAM_NAME);
				len = -EFAULT;
				goto out;
			}

			if (callback_error) {
				printk("error on callback: %lx\n", callback_error);
				len = -EFAULT;
				goto out;
			}
			up(&client_semaphore);
		
		#else /* MAP_USER_PAGES_RECV */

			//printk("\nstring recv:%s\n", (char *)(data+(start & (PAGE_SIZE - 1))));
			/* 
			 * This just a contingous local buffer written to by the remote computer.
			 * TODO: this should wrap around like a ring buffer.
			 * Copy from local kernel pages as the data is already written there. 
			 */
			if(copy_to_iter((char *)(physical_recv_aperture + ltcp_msg->offset_from_aperture), len, &msg->msg_iter) != len){
				len = -EFAULT;
				goto out;
			}

			ltcp_connection.recv_queue->phys_end += len;
			/* 
			 * wrap
			 */
			if(ltcp_connection.send_queue->phys_end >= APERTURE_SIZE_QUEUE)
				ltcp_connection.send_queue->phys_end = ltcp_connection.send_queue->phys_end - APERTURE_SIZE_QUEUE;
			
        #endif /* ! MAP_USER_PAGES_RECV */
	
		/* 
		 * TODO: handle paged data (non linear). see: http://vger.kernel.org/~davem/data.html 
		 */

		break;

	} while (1);


	/*
	 * Release the allocated pages.
	 */
	vfree(pages);
	
out:
	if(skb){
		skb_unlink(skb, &sk->sk_receive_queue);
		consume_skb(skb);
	}
	release_sock(sk);
	return len;
}
EXPORT_SYMBOL(local_tcp_recvmsg);


/* 
 * Backlog function. 
 */
int local_tcp_do_rcv(struct sock * sk, struct sk_buff * skb){
	#ifdef LTCP_DEBUG
	printk("Adding the message to the socket recv queue. \n ");
	#endif

	skb_queue_tail(&sk->sk_receive_queue, skb);
	#ifdef LTCP_DEBUG
	printk("Notifying the socket that there is data in the sk queue \n");
	#endif
	sk->sk_data_ready(sk);
	return 0;
}


/* 
 * Should look at the sockets destination address and see if it is registered as one of the RDMA nodes. 
 * Should return 0 if the socket is remote and identifying int if the socket is local . 
 */
int check_if_local_or_remote(unsigned long daddr){
	/*
	 * Should get the socket ip adress and at list of the ip address that are connected with RDMA. 
	 * Simply loop through and return position if found and 0 if not. 
	 */ 
	int i;
	for(i=0; i < MAX_NUMBER_OF_LOCAL_RDMA_IP; i++){
		if(daddr == list_of_local_ips[i])
			return i;
	}
	return 0; 
}

int myproto_err(struct sk_buff *skb, u32 info){
    printk(KERN_INFO "myproto_err is called\n");
    return 0;
}


int local_tcp_lib_hash(struct sock *sk){
    printk(KERN_INFO "local_tcp_lib_hash() is called\n");
	return 0;
}
EXPORT_SYMBOL_GPL(local_tcp_lib_hash);



int local_tcp_init(struct sock *sk){
	return 0; 
}





static struct proto ltcp_proto = { 
    .name = "local_TCP", 
    .owner = THIS_MODULE, 
    .obj_size = sizeof(struct ltcp_sock),
	.sendmsg		= local_tcp_sendmsg,
	.recvmsg		= local_tcp_recvmsg,
	.backlog_rcv 	= local_tcp_do_rcv,
	.hash 			= local_tcp_lib_hash,
	.init			= local_tcp_init,
};

struct inet_protosw ltcp_protosw;



/*
 * My understanding is that this is where the socket buffers will arrive from the IP-layer. 
 * So this will only be used if testing with ethernet. 
 * TODO: It is important that the RDMA solution also will add new connections to the structure.
 */
int local_tcp_rcv(struct sk_buff *skb){

	__u16 rport, lport;
	__u32 rip, lip, type;
	struct port_sk * psk;
	struct sock * clone_sk;
	struct sock * listening_sk;
	struct ltcp_sock * conn_sk;
	struct inet_connection_sock *newicsk;
	
	#ifdef LTCP_DEBUG
	printk("Received an skb\n");
	#endif

	//TODO: handle a bit different for ethernet now...

	struct ltcp_queue_msg *ltcp_msg;
	memcpy(&ltcp_msg, skb_transport_header(skb), sizeof(struct ltcp_queue_msg *));

	rport =  ltcp_msg->dest_port;
	lport =  ltcp_msg->src_port; //emm, here I have done something weird. Should have been rport.
	rip = ip_hdr(skb)->saddr;
	lip = ip_hdr(skb)->daddr;
	type = ltcp_msg->type;

	// printk("transport: ip-src: %u, ip-dst:%u, src-port: %u, dst-port: %u. Type: %u\n", (unsigned int) rip, (unsigned int) lip, (unsigned int) rport, (unsigned int) lport, (unsigned int)type);
	
	/* 
	 * Handle the socket buffer based on the type of the ltcp segment. 
	 */
	switch(type){
	
		case REGULAR:
			#ifdef LTCP_DEBUG
			printk("Received a regular packet. \n");
			#endif
			/* 
			 * Get the socket that should receive this skb. 
			 */

			 //TODO: should try again ? t
			psk = get_port_sk(conns, lport, rport, rip);
			
			if(!psk){
				printk("Connections could not be found in open connections\n");
				/*
				 * If the connections could not be found in the waiting connections either it must be discarded. 
				 * TODO: Should we inform the sender about this? 
				 */
				return -1;
			}
			
			if(!psk->sk){
				printk("Couldn't find the local tcp socket in port_sk arrays\n");
				return -1;
			}		
			
			/* 
			 * We add the skb to the receive queue of the socket.
			 */
			skb->sk = (struct sock *) psk->sk;
			local_tcp_do_rcv(skb->sk, skb); 

			// TODO: check buffer and send back more credit?
			break;

		case CONNECT:

			#ifdef LTCP_DEBUG
			printk("Received a connection packet\n");
			#endif

			listening_sk = get_listening_sk_port_sk_arr(incoming_conns, lport);
			
			if(!listening_sk){
				printk("No socket is listening at this port \n");
				return 0;
			}	

			/* 
			 * This will check with already opened connections if there exist a socket with these specs.
			 */
			if(get_port_sk(conns, lport, rport, rip)) {
				printk("Connections already exist\n");
				//TODO: send back connection already exists to the connector. 
				return 0;
			}

			//TODO: also check with incoming_conns if already trying to connect? 


			/*
			 * The new connection will need a new local tcp socket.
			 */
			// printk("cloning socket\n");
			clone_sk = sk_clone_lock(listening_sk, GFP_ATOMIC); //GFP_KERNEL?


			// printk("setting socket values...\n");
			newicsk = inet_csk(clone_sk);
			
			inet_sk_set_state(clone_sk, TCP_SYN_RECV);// TODO: is this correct?
			newicsk->icsk_bind_hash = NULL;
			inet_sk(clone_sk)->inet_dport = rport;  	//inet_rsk(req)->ir_rmt_port;
			inet_sk(clone_sk)->inet_num = ntohs(lport);   		//? inet_rsk(req)->ir_num;
			inet_sk(clone_sk)->inet_sport = lport;  	//htons(inet_rsk(req)->ir_num);
			inet_sk(clone_sk)->inet_daddr = rip;   		//? inet_rsk(req)->ir_num;   
			inet_sk(clone_sk)->inet_saddr = lip;  	//htons(inet_rsk(req)->ir_num);

			/* listeners have SOCK_RCU_FREE, not the children */
			sock_reset_flag(clone_sk, SOCK_RCU_FREE);
			inet_sk(clone_sk)->mc_list = NULL;
			newicsk->icsk_retransmits = 0;
			newicsk->icsk_backoff	  = 0;
			newicsk->icsk_probes_out  = 0;

			/* Deinitialize accept_queue to trap illegal accesses. */
			memset(&newicsk->icsk_accept_queue, 0, sizeof(newicsk->icsk_accept_queue));
		
			clone_sk->sk_protocol = IPPROTO_LOCAL_TCP;
			clone_sk->sk_socket = NULL;
			clone_sk->sk_prot = &ltcp_proto;
			clone_sk->sk_prot_creator = &ltcp_proto;

			// printk("unlocking socket\n");
			bh_unlock_sock(clone_sk);
			if(clone_sk < 0){
				printk("Failed to create ltcp socket\n");
				return -1;
			}

			conn_sk = ltcp_sk(clone_sk);

			if(!conn_sk){
				printk("Couldn't find ltcp_sk in the socket created. \n");
				return -1;
			}
			
			/* 
			 * Allocate space for a new port_sock structure to store in the queue of incomming connection.
			 */
			psk = kmalloc(sizeof(struct port_sk), GFP_ATOMIC); //remember to free. //TODO: faster to have an already allocated structure.
			if(!psk){
				printk("Failed to allocate kernel memory for port_sk struct \n");
				return -1;
			}


		
			psk->sk = conn_sk; 
			psk->rport = rport;
			psk->rip = rip; 
			
	
			/*  
			 * Add the socket in queue of incomming connections.
			 * TODO: NOTE!! I have added it an already setup connection right away to avoid issues where regular packets are handled before accept is called. 
			 */
			add_port_sk(incoming_conns, lport, psk);

			#ifdef LTCP_DEBUG
			printk("\nAdded connection:\n\tremote_ip\t\t%d\n\tremote_port\t\t%d\n\tlocal_port\t\t%d\n\n", ntohl(rip), ntohs(rport), ntohs(lport));			
			#endif

			/* 
			 * Wake up the listening socket such that is can handle the new connection. 
			 */
			#ifdef LTCP_DEBUG
			printk("Waking up listening socket at port %d\n", ntohs(lport));
			#endif
			wake_up_listening_sock(incoming_conns, lport);
			//TODO: send back connection created msg?
			break; 

		case CLOSE: 
			

			
			psk = remove_port_sk(conns, lport, rport, rip);
			// /* 
			// 	* Send something up to program ?
			// 	*/
			inet_sk_set_state((struct sock *) psk->sk, TCP_CLOSE);
				
			if(psk)
				kfree(psk);

			break; 


		case CONNECT_SUCCESS:

			psk = remove_port_sk(incoming_conns, lport, rport, rip);
			add_port_sk(conns, lport, psk);




			break;
		
		default: 
			printk("UNKNOWN LOCAL TCP TYPE! \n");
			return -1; 
	}

    return 0;
}
EXPORT_SYMBOL(local_tcp_rcv);


const struct net_protocol ltcp_protocol = {
    .handler = local_tcp_rcv,
    .err_handler = myproto_err,
    .no_policy = 1,
    .netns_ok = 1,
};

int register_ltcp_protocol(void){
	int err;

	/*
	 *Register our new protocol with the kernel. 
	 */
   	err = proto_register(&ltcp_proto, 0);
	if(err){
		printk("proto_register failed \n");
		return err;
	}
	err = inet_add_protocol(&ltcp_protocol, IPPROTO_LOCAL_TCP);
	if (err < 0){
		pr_crit("%s: Unable to add local TCP protocol\n", __func__);
		return err;
	}

	
	memset(&ltcp_protosw, 0, sizeof(ltcp_protosw));
    ltcp_protosw.type = SOCK_RAW;
    ltcp_protosw.protocol = IPPROTO_LOCAL_TCP;
    ltcp_protosw.prot = &ltcp_proto;


	// TODO: REPLACE WITH OWN PROTOCOL OPTIONS!? Also, should look for issues with using inet_dgram_ops also how this works in the combination with sock raw. 
    ltcp_protosw.ops = &inet_dgram_ops;  
    ltcp_protosw.flags = INET_PROTOSW_REUSE;
    inet_register_protosw(&ltcp_protosw);
    
	return 0;
}


void ltcp_tcp_close(struct sock *sk, long timeout){
	struct port_sk* psk;
	int err; 

	if((int) inet_sk(sk)->inet_daddr == (int) dest_IP){
		if (sk->sk_state == TCP_CLOSE)
			return;

		printk("closing sk:  \n\tsport %x\n\tdport %x\n\trip %x\n\t", (int)inet_sk(sk)->inet_sport, (int)inet_sk(sk)->inet_dport, (int) inet_sk(sk)->inet_daddr);

		psk = remove_port_sk(conns, inet_sk(sk)->inet_sport, inet_sk(sk)->inet_dport,  inet_sk(sk)->inet_daddr);
		
		if(psk){
			if(psk->sk){
				/* 
				* Tell remote computer we are closing the connection.
				s*/
				err = local_tcp_sendmsg_helper(psk->sk, NULL, 0, CLOSE);
				if(err){
					printk("Failed to send CLOSE message\n");
				}
			}
		}
	}else{
		tcp_close(sk, timeout);
	}

	// if(psk)
	// 	kfree(psk);

	

	/* 
	 * TODO: proper socket release -> future work.  See how this is done in the tcp_close function
	 */

}





/*
 * Remove ltcp protocol.  
 */
int unregister_ltcp_protocol(void){
	int err;
	// Register our new protocol with the kernel. 
   	proto_unregister(&ltcp_proto);

	err = inet_del_protocol(&ltcp_protocol, IPPROTO_LOCAL_TCP);
	
	if (err < 0){
		pr_crit("%s: Cannot del local TCP protocol\n", __func__);
		return err;
	}
	 
    inet_unregister_protosw(&ltcp_protosw);
	printk("Finnished unregister function\n");
	return 0;
}






/*
* Callback for iterate_fd().
*
* If given file corresponds to the given socket, return fd + 1.
* Otherwise return 0.
*
* Note, that returning 0 is needed for continue the search.
*/
int check_file_is_sock(const void* s, struct file* f, unsigned int fd){
	int err;
	struct socket * real_sock;
	real_sock = sock_from_file(f, &err);
	if(real_sock == s)
		return fd + 1;
	return 0;
}





// Return file descriptor for given socket in given process.
int get_fd_for_sock(struct socket* s, struct task_struct* p)
{
	int search_res;
	task_lock(p);
	// This returns either (fd + 1) or 0 if not found.
	search_res = iterate_fd(p->files, 0, &check_file_is_sock, s);
	task_unlock(p);

	if(search_res)
		return search_res - 1;
	else
		return - 1; // Not found
}








int replace_socket_filedescriptor(struct socket * sock, struct socket * old_sock, int flags){

	struct file *newfile;
	struct fdtable *fdt;
	struct file *file;
	int old_fd;

	old_fd = get_fd_for_sock(old_sock, current);

	if(old_fd < 0){
		printk("Couldn't find filedescriptor for this socket.\n");
		return old_fd;
	}


	/* 
	* Need to allocate a new file entry for the new socket (?)
	*/
	newfile = sock_alloc_file(sock, flags, NULL);
	if(!newfile){
		printk("Failed to create newfile entry for the new socket.");
		return -1;
	}
	printk("Created file entry for socket: %d\n", old_fd);


	/*
	* The new lines is almost equalt to calling  __close_fd(current->files, old_fd); and then assigning the pointer again. Maybe write something about why this wouldn't work.
	*/

	
	/*
	* Lock others threads from accessing the open filedescriptors. 
	*/
	spin_lock(&current->files->file_lock);
	
	/* 
	* Fetch the open file descriptor table.
	*/
	fdt = files_fdtable(current->files);

	/* 
	* TODO: Maybe remove? Nice to check, but not sure this will ever be the case. 
	*/
	if (old_fd >= fdt->max_fds){
		printk("Fd too high\n");
		spin_unlock(&current->files->file_lock);
		return -1;
	}

	/*
	* Fetch the open file decriptor entry for the old file descriptor and make sure it exists. TODO: these comments might need some adjustments.
	*/
	file = fdt->fd[old_fd];
	if (!file){
		printk("File not found\n");
		spin_unlock(&current->files->file_lock);
		return -1; 
	}

	/* 
	* Reassign the file pointer.
	*/
	rcu_assign_pointer(fdt->fd[old_fd], newfile);
	spin_unlock(&current->files->file_lock);

	/*
	* Close and release resources of the old file pointer. 
	*/
	filp_close(file, current->files);

	return 0;
}








int ltcp_tcp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len, struct socket ** sock){

	int ret;
	int flags;
	int type;
	__u16 lport;
	struct sockaddr_in * usin;
	struct msghdr msg;
	struct port_sk * psk;
	struct inet_sock * inet;
	struct ltcp_sock * conn_sk;

	ret = 0;


	// /* Should place a mark for this socket here if possible to save time so it won't have to be checked everytime when sendmsg is called. */
	// printk(KERN_ALERT "Local connection. \n");
	


	/*
	* Create a new socket. 
	*/
	flags = SOCK_RAW & ~SOCK_TYPE_MASK;
	type = SOCK_RAW & SOCK_TYPE_MASK;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	//TODO: check if maybe type should just be sock_raw
	ret = sock_create(AF_INET, type, IPPROTO_LOCAL_TCP, sock);

	if (ret < 0){
		pr_crit("%s: Failed to create local TCP socket. \n", __func__);
		printk("Err: %d \n", ret);
		return ret;
	}
	#ifdef LTCP_DEBUG
	printk("Successfully created socket\n");
	#endif

	/*Get local TCP connection socket.*/
	conn_sk = ltcp_sk((*sock)->sk);

	if(!conn_sk){
		printk(KERN_ALERT "conn_sk is null \n ");
		return -1; 
	}


	if(replace_socket_filedescriptor(*sock, sk->sk_socket, flags)){
		printk(KERN_ALERT "Failed to replace filedescriptor. \n");
		return -1;
	}


	/*
	* Get the AF_INET sockaddr structure.
	*/
	usin = (struct sockaddr_in *) uaddr;

	/*
	 * Get port listening socket.
	 */
	//TODO: TODO: TODO: THIS IS SUPPOSE TO BE A UNIQUE PORT PRODUCED HERE. We are using a temp fix.
	//inet = inet_sk(sk);
	lport = usin->sin_port; 
	

	printk("local port %d, should be : %d", (int)lport,(int) htons(8083));

	/* 
	* Set the ports and ip addresses in the sock struct.
	*/
	inet = (struct inet_sock *) conn_sk;
	inet->inet_sport = lport;  
	inet->inet_dport = usin->sin_port;
	inet->inet_daddr = usin->sin_addr.s_addr; // s_addr does not mean source addr! 
	inet->inet_saddr = src_IP; 
	


	if(!inet->inet_saddr){
		printk(KERN_ALERT "Missing inet source addr. \n");
		return -1;
	}
	if(!inet->inet_daddr){
		printk(KERN_ALERT "Missing inet destination addr. )\n");
		return -1;
	}

	/* 
	* Create skb for the connect message.
	*/
	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_name = uaddr;
	msg.msg_namelen = addr_len;

	/* 
	* Allocate space for a new port_sk structure to store in the queue of connections.
	* NOTE: The connection is added directly into the conns queue as if it were already approved by the receiver. 
	*/
	psk = kmalloc(sizeof(struct port_sk), GFP_ATOMIC); //remember to free.
	if(!psk){
		printk(KERN_ALERT "Failed to allocate kernel memory for port_sk struct \n");
		return -1;
	}

	
	psk->sk = conn_sk; 
	psk->rport = usin->sin_port; 
	psk->rip = inet->inet_daddr; 


	/*Send empty message of type CONNECT.*/
	ret = local_tcp_sendmsg_helper(conn_sk, &msg, 0, CONNECT);

	if(ret){
		printk(KERN_ALERT "Failed to send connect message \n");
		return ret;
	}


	/*  
	* Add the socket in queue of connections. // For now we assume the connections will be set up successfully.
	* TODO: wait to put put it into the conns until we receive a confirmation from the remote node that connection is setup?
	*/
	add_port_sk(incoming_conns, lport, psk);

	return ret;
}
EXPORT_SYMBOL(ltcp_tcp_v4_connect);







int ltcp_tcp_v4_connect_wrapper(struct sock *sk, struct sockaddr *uaddr, int addr_len){
	int ret; 
	bool local = false; 
	struct sockaddr_in * usin;
	usin = (struct sockaddr_in *) uaddr;

	/*
	 * Here I must decide if destination IP is local or remote. (if Connected to RDMA or not.)
	 */

	/*
	 * Temporary check if it is from the program I created.
	 * TODO: port check should be removed once running based on correct IP.  
	 */
	#ifdef LTCP_DEBUG
	printk("usin_ip:%u, dest_ip:%u, src_ip:%u, usin_port: %u, dest_port:%u", (unsigned int)usin->sin_addr.s_addr, (unsigned int) dest_IP, (unsigned int) src_IP, (unsigned int)usin->sin_port, (unsigned int)htons(8083));
	#endif

	//TODO:
	// int local = check_if_local_or_remote(uaddr->sin_addr->s_addr);
	if((int) usin->sin_addr.s_addr == (int) dest_IP)
		local = true;
	

	if(local){
		struct socket * sock;

		#ifdef LTCP_DEBUG
		printk("Kall på local TCP connect\n");
		#endif

		ret = ltcp_tcp_v4_connect(sk, uaddr, addr_len, &sock);
			
		/* will cause the __inet_stream_connect function 
		* (which called tcp_connect) to return successfully if this this function return successfully.
		*
		* Therefore we are changing defer_connect of sk and not the new sock->sk. 
		*
		* NOTE, we can possible also change the inet_stream_ops where inet_stream_connect is set at the connect method.
		*		This can be done by defining a new connect method as done with tcp_connect in the tcp_prot struct. 
		*
		*/
		if(ret>=0){
			inet_sk(sk)->defer_connect = 1;
			sock->state = SS_CONNECTING;
			ret = 0;
		}
		
	}else{
		ret = tcp_v4_connect(sk, uaddr, addr_len);	
	}

	return ret;
}






int wait_on_connection(struct sock * sk, int flags){
	__u16 lport;
	int err;
	struct inet_connection_sock *icsk;
	struct request_sock_queue *queue; 
	long timeo;
	
	icsk = inet_csk(sk);
	queue = &icsk->icsk_accept_queue;
	lport = inet_sk(sk)->inet_sport; 



	if(reqsk_queue_empty(queue) && portsk_queue_empty(incoming_conns, lport)){
	
		/* 
		 * This defines the sockets timeout time. 
		 */
		timeo = sock_rcvtimeo(sk, (flags & O_NONBLOCK) != 0);


		/* If this is a non blocking socket don't sleep */
		if (!timeo){
			#ifdef LTCP_DEBUG
			printk("Non blocking socket return \n");
			#endif
			
			return -EAGAIN;
		}

		/*TODO:  Would maybe make sense to add this on bind instead such that we will only have to do it once.*/
		//TODO:  What if there is already a listening socket there.
		add_listening_sk_port_sk_arr(incoming_conns, lport, sk);
		

		/*
		* Not sure if this is needed.
		*/
		// printk("locking socket\n");
		lock_sock(sk);
		DEFINE_WAIT(wait);

		/*
		* wake-one mechanism for incoming connections: only
		* one process gets woken up, not the 'whole herd'. 
		* 
		* When wake_up() is called 1 of the exclusive waiter will be woken up and all non-exclusive waiters will be woken up.
		*
		* Subtle issue: "add_wait_queue_exclusive()" will be added
		* after any current non-exclusive waiters, and we know that
		* it will always _stay_ after any new non-exclusive waiters
		* because all non-exclusive waiters are added at the
		* beginning of the wait-queue. As such, it's ok to "drop"
		* our exclusiveness temporarily when we get woken up without
		* having to remove and re-insert us on the wait queue.


		* See inet_csk_wait_for_connect()
		*/
		
		for (;;) {
			/*
			 * Wait in the listening socket wq.
			 */
			prepare_to_wait_exclusive(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

			release_sock(sk);
			if (reqsk_queue_empty(&icsk->icsk_accept_queue) && portsk_queue_empty(incoming_conns, lport))
				timeo = schedule_timeout(timeo);
			
			sched_annotate_sleep();

			lock_sock(sk);
			err = 0;
			if (!reqsk_queue_empty(&icsk->icsk_accept_queue) || !portsk_queue_empty(incoming_conns, lport))
				break;
			err = -EINVAL;
			if (sk->sk_state != TCP_LISTEN)
				break;
			err = sock_intr_errno(timeo);
			if (signal_pending(current))
				break;

			err = -EAGAIN;
			if (!timeo)
				break;
		}

		finish_wait(sk_sleep(sk), &wait);
		release_sock(sk);

		if (err){
			printk(KERN_ALERT "Error occured duing wait \n");
			return err;
		}
	}
	return 0;
}








/*
 * TODO: handle kernel socket?
 * Make sure it returns with the correct return value.
 */
struct sock * ltcp_inet_csk_accept(struct sock *sk, int flags, int *err, bool kern){
	
	// struct sock * retsock;
	__u16 lport;
	struct port_sk * psk;
	struct inet_connection_sock *icsk;
	struct request_sock_queue *queue; 
	struct sock * psksk;

	
	icsk = inet_csk(sk);
	queue = &icsk->icsk_accept_queue;

	/*
	 * Get the local port number for the listening socket.
	 */
	lport = inet_sk(sk)->inet_sport; 

	//TODO make wait_on_connection function.
	/*
	* If none of them includes a socket we must make it wait. 
	*/
	*err = wait_on_connection(sk, flags);

	if(*err)
		return NULL;


	if(reqsk_queue_empty(queue)){

		// printk("reqsk_queue is empty so checking for local tcp connection\n");


		/*
		* Remove a connection from the local incomming accept queue. 
		*/
		psk = put_port_sk(incoming_conns, lport);


		/* 
		* Check if local connection was found. 
		*/
		if(psk){
			
			/*
			 * Could maybe remove this check, but want to see that is never actually happens. 
			 */
			if(!psk->sk){
				pr_crit("%s: Connection socket in port_sk structure is NULL. \n", __func__);
				return NULL;
			}

			/*
			 * Add the connetion to the fully setup connections.
			 */
			add_port_sk(conns, lport, psk);

			/*
			 * Notify the sender that the connection has been setup. 
			 */
			*err = local_tcp_sendmsg_helper(psk->sk, NULL, 0, CONNECT_SUCCESS);
			if(*err){
				printk(KERN_ALERT "Failed to send CONNECT_SUCCESS message \n");
				return NULL;
			}

			// printk("psk-rip: %u, psk-rport:%u, lport: %u\n", (unsigned int)ntohl(psk->rip), (unsigned int) ntohs(psk->rport), (unsigned int)ntohs(lport));

			*err = 0;
			psksk =  (struct sock *) psk->sk;
			return psksk;
		}

		/*
		* Could maybe remove this check, but want to see that is never actually happens. 
		*/
		pr_crit("%s: port_sk pointer is NULL. This might happen on timeout.  \n", __func__);
		*err = -EAGAIN;
		return NULL;

		/*  
		* If no new local connections found for this port return a remote.  
		* NOTE: We pass the O_NONBLOCK flag here! This will make the accept call instantly return if there is nothing in the queue. 
		*/
	}else{
		// printk("remote connection to listening socket: %d \n" , (unsigned int) ntohs(lport));
		return inet_csk_accept(sk, flags | O_NONBLOCK, err, kern);	
	}
}
EXPORT_SYMBOL(ltcp_inet_csk_accept);


int local_tcp_module_init_function(void){

	register_ltcp_protocol();
	
	printk("Node id:\t\t\t\t%d\n", (int) node_id);
	printk("src IP:\t\t\t\t%s\n", src);
	printk("dest IP:\t\t\t\t%s\n", dest);

	dest_IP = inet_addr(dest); 
	src_IP 	= inet_addr(src);
	org_tcp_prot.connect = tcp_prot.connect;
	org_tcp_prot.accept = tcp_prot.accept;
	org_tcp_prot.close = tcp_prot.close;
    tcp_prot.connect = ltcp_tcp_v4_connect_wrapper;
    tcp_prot.accept = ltcp_inet_csk_accept;
    tcp_prot.close = ltcp_tcp_close;
    return 0;
}
EXPORT_SYMBOL(local_tcp_module_init_function);


void local_tcp_module_exit_function(void){
	
	unregister_ltcp_protocol();
    tcp_prot.connect = org_tcp_prot.connect;
    tcp_prot.accept = org_tcp_prot.accept;
	tcp_prot.close = org_tcp_prot.close;

}
EXPORT_SYMBOL(local_tcp_module_exit_function);



/*
 * This should be commented out when running with rdma.
 */
#ifdef LINK_IS_ETHERNET
module_init(local_tcp_module_init_function);
module_exit(local_tcp_module_exit_function);
MODULE_AUTHOR("Dolphin ICS");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module for local tcp protocol");
#endif
