


int ethernet_send(struct inet_sock *sk, struct msghdr *msg, size_t len, __u32 type){

	/* 
	* If lost on the way to the device, can I have a sequence number just between device driver and protocol?
	*
	* Must use the socket to find the correct place to send the message. Maybe just skip the entire ip layer etc? 
	*
	* TODO: Check that it is actually connected.
	*/
	struct sk_buff* skb;
	struct local_tcp_header * lth; 
	int err;

	/*
	* Allocate an sk_buff with enough space for the maximum size of headers and data.
	*/
	skb = alloc_skb(LTCP_MAX_HEADER_SIZE + len, GFP_ATOMIC);
	if(!skb){
		printk("Failed to allocate sk_buff\n");
		return -1;
	}

	/*
	* Reserve space for the headers.
	*/
	skb_reserve(skb, LTCP_MAX_HEADER_SIZE);

	/*
	* Copy the data into the sk_buff. 
	*/
	copy_from_iter(skb_put(skb, len), len,  &msg->msg_iter);


	/*
	* Move/push the data pointer up and steal space from the head section.
	* Fill the space with the transport header.  
	*/
	skb_push(skb, sizeof(struct local_tcp_header));
	
	/*
	* Set the current data pointer to be the transport header pointer.
	*/
	skb_reset_transport_header(skb);
	lth = (struct local_tcp_header *) skb->data;



	/*
	* Set some variables.
	*/
	skb->pfmemalloc = 0;
	skb_set_owner_w(skb, (struct sock *) sk);
	skb->ip_summed = CHECKSUM_NONE;


	/*
	* Set the transport header. TODO: this does not care about fragements and etc. Look more at the ip_make_skb code above.
	*/
	lth->sport = inet->inet_sport; 
	lth->dport = inet->inet_dport;
	lth->type = type; 
	lth->credits = 0; // not in use (yet ?)

	#ifdef LTCP_DEBUG
	printk("ip-src: %u, ip-dst:%u, src-port: %u, dst-port: %u\n", (unsigned int)ntohl(inet->inet_saddr),(unsigned int) ntohl(inet->inet_daddr), (unsigned int)ntohs(inet->inet_sport), (unsigned int)ntohs(inet->inet_dport));
	#endif

	err = ip_queue_xmit((struct sock *) sk, skb, &inet->cork.fl);
	
	if(err<0){
		printk("Error on queue xmit. Err: %d\n", err);
		return err; 
	}	

	return 0;
}

