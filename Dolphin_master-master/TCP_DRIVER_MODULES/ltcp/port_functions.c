
struct port_sk * remove_port_sk(struct port_sk_arr * arr, __u16 lport, __u16 rport, __u32 rip){
	struct port_sk_arr * conn_arr;
	struct port_sk* current_psk;
	
	struct port_sk** head;
	struct port_sk** tail;

	conn_arr = &(arr[ntohs(lport)]); 
	spin_lock(&conn_arr->lock);
		
	head = &conn_arr->head; 
	tail = &conn_arr->tail;

	current_psk = conn_arr->head; 
	
	while(current_psk != NULL){

		if (current_psk->rport == rport && current_psk->rip == rip){
			
			if(current_psk == conn_arr->head){
				if(conn_arr->head->next){
					current_psk->next->previous = NULL;
					*head=(*head)->next;
				}else{
					*head = NULL;
					*tail = NULL;
				}
			}else if(current_psk == conn_arr->tail){
				current_psk->previous->next = NULL;
			}else{
				current_psk->previous->next = current_psk->next;
				current_psk->next->previous = current_psk->previous;
			}

			spin_unlock(&conn_arr->lock);
			return current_psk;
		}

		current_psk = current_psk->next;
	}
	#ifdef LTCP_DEBUG
	printk("Could not find a port_sk. \n");
	#endif
	spin_unlock(&conn_arr->lock);
	return NULL;
}


struct port_sk * put_port_sk(struct port_sk_arr * arr, __u16 lport){
	struct port_sk_arr * conn_arr;
	struct port_sk** head;
	struct port_sk** tail;
	struct port_sk* old_head;

	conn_arr = &(arr[ntohs(lport)]); 
	
	spin_lock(&conn_arr->lock);

	head = &conn_arr->head; 
	tail = &conn_arr->tail; 

	if(*head){
		
		old_head = *head;
	
		if((*head)->next){
			*head = (*head)->next;
			(*head)->previous = NULL;
		}else{
			*head = NULL;
			*tail = NULL;
		}

		spin_unlock(&conn_arr->lock);
		return old_head;	
	}

	spin_unlock(&conn_arr->lock);
	return NULL;
}








void add_port_sk(struct port_sk_arr * arr, __u16 lport, struct port_sk * psk){
	struct port_sk_arr * conn_arr;
	struct port_sk** head;
	struct port_sk** tail;
	struct port_sk* current_tail;


	if(!psk){
		printk("Trying to add a nullpointer. \n");
		return;
	}

	conn_arr = &(arr[ntohs(lport)]); 
	spin_lock(&conn_arr->lock);

	head = &conn_arr->head; 
	tail = &conn_arr->tail; 

	psk->next = NULL;
	current_tail = *tail;

	if(current_tail){
		psk->previous = current_tail;
		current_tail->next = psk;
	}else{
		psk->previous = NULL;
		*head = psk;
	}
	*tail = psk;
	spin_unlock(&conn_arr->lock);
}








/* 
 * Get the port_sk struct for this connection. 
 */
struct port_sk * get_port_sk(struct port_sk_arr * arr, __u16 lport, __u16 rport, __u32 rip){

	struct port_sk_arr * conn_arr;
	struct port_sk* current_psk;

	conn_arr = &(arr[ntohs(lport)]); 
	spin_lock(&conn_arr->lock);
	
	current_psk = conn_arr->head; 
	
	while(current_psk != NULL){

		if (current_psk->rport == rport && current_psk->rip == rip){
			spin_unlock(&conn_arr->lock);
			return current_psk;
		}

		current_psk = current_psk->next;
	}
	#ifdef LTCP_DEBUG
	printk("Could not find a port_sk. \n");
	#endif
	spin_unlock(&conn_arr->lock);
	return NULL;
}





/*
 * Should check if the accept queue for this queue is empty.
 */
bool portsk_queue_empty(struct port_sk_arr * arr, __u16 lport){
	bool res; 
	res =  arr[ntohs(lport)].head ? false : true;
	return res;
}


void add_listening_sk_port_sk_arr(struct port_sk_arr* arr,__u16 lport, struct sock * ls){
	struct port_sk_arr * conn_arr;
	conn_arr = &(arr[ntohs(lport)]); 

	#ifdef LTCP_DEBUG
		printk("Adding listening socket at port: %d \n", (int) ntohs(lport));
	#endif
	
	spin_lock(&conn_arr->lock);
	conn_arr->listening_sock = ls;
	spin_unlock(&conn_arr->lock);
}




struct sock * get_listening_sk_port_sk_arr(struct port_sk_arr* arr,__u16 lport){
	return arr[ntohs(lport)].listening_sock; 
}




/* 
 * This should wake up the listening socket at this port.
 * TODO: lock?
 */
void wake_up_listening_sock(struct port_sk_arr* arr, __u16 lport){
	struct port_sk_arr * conn_arr;
	struct sock * ls;
	conn_arr = &(arr[ntohs(lport)]); 
	spin_lock(&conn_arr->lock);
	ls = conn_arr->listening_sock; 
	if(ls){
		wake_up(sk_sleep(ls));
	}else{
		printk(KERN_ALERT "Could not find a listening socket at port %d \n", (int) ntohs(lport));
	}
	spin_unlock(&conn_arr->lock);
}



