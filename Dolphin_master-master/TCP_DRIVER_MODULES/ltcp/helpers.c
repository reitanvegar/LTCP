

/* 
 * Convert from dot notation. 
 */
unsigned long inet_addr(char *str)
{
	unsigned int a,b,c,d;
	unsigned char arr[4];
	sscanf(str,"%d.%d.%d.%d",&a,&b,&c,&d);
	arr[0] = (unsigned char) a; arr[1] = (unsigned char)b; arr[2] = (unsigned char)c; arr[3] = (unsigned char)d;
	return *(unsigned long*)arr;
} 


/* 
 * Convert sock pointers to ltcp sock pointer. 
 */
struct ltcp_sock * ltcp_sk(void * sk){
	return (struct ltcp_sock *) sk;
}

struct local_tcp_header * ltcp_hdr(void * data){
	return (struct local_tcp_header *) data;
}

