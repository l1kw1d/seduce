/* 
 * Functions for communicating with the Manager (Server)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <nids.h>

#include "server_contact.h"

#ifdef TWO_TIER_ARCH
#include "../manager/sensor_contact.h"
#endif

/* 
 * Returns the next available ID
 */
static unsigned int get_new_id(void)
{
       static unsigned int next_id=1;

       if (next_id == 0) next_id = 1;

       return next_id++;
}


/*
 * Initializes a new stream id
 */
static unsigned int *init_stream(void)
{
       unsigned int *stream_id;
       
       stream_id = malloc(sizeof(unsigned int));
       if (stream_id == NULL) return NULL;

       *stream_id =  get_new_id();
       return stream_id;
}

/* This function makes sure all the wanted data are sent.
 * It takes as arguments the socket file descriptor, the
 * buffer and the length. It returns 0 on success, -1 on
 * failure.
 */ 
static int sendall(int s, const void *buf, size_t len)
{
	int total = 0;
	size_t bytesleft = len;
	ssize_t n = -1; /* what if len == 0 ? */

	while (total < len) {
		n = send(s, buf+total, bytesleft, 0);
		if (n == -1)
			break;
		total += n;
		bytesleft -= n;
	}

	return (n == -1) ? -1 : total;
}


/* The packet types */
enum {
       PT_CONNECT = 0,
       PT_CLOSE,
       PT_NEW_TCP,
       PT_TCP_CLOSE,
       PT_TCP_DATA,
       PT_TCP_BREAK,
       PT_UDP_DATA
};


/*
 * Connects to the Manager 
 * (also initialises sockfd with a newly created socket)
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0________4________8
 * |  size  | packet |
 * |________|__type__|
 *
 */
int manager_connect(int *sockfd, in_addr_t addr,unsigned short port)
{
#ifndef TWO_TIER_ARCH
	struct sockaddr_in server_addr;
	unsigned int msglen;
	char buf[8];
	int bytenum;
	unsigned int size,reply;

	if ((*sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		return 0;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = addr;
	memset(&(server_addr.sin_zero), '\0', 8);

	if (connect(*sockfd, (struct sockaddr *)&server_addr,
		    sizeof(struct sockaddr)) == -1) 
	{
		perror("connect");
		close(*sockfd);
		return 0;
	}

	/* fill the packet_buffer with data in Network Byte Order */
	msglen = 8;
	*(u_int32_t *)(buf + 0) = htonl(msglen);
	*(u_int32_t *)(buf + 4) = htonl(PT_CONNECT);

	/* send the data */
	if (sendall(*sockfd, buf, msglen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		close(*sockfd);
		return 0;
	}
	
	/* Wait for the answer......*/
	/* Wait to receive the expected reply length */
	msglen = 8;
	bytenum = recv(*sockfd, buf, msglen, MSG_WAITALL);
	if (bytenum == -1) {
		perror("recv");
		close(*sockfd);
		return 0;
	}

	if (bytenum != msglen) {
		fprintf(stderr, "Error in receiving data\n");
		close(*sockfd);
	       	return 0;
	}	       

	size = ntohl(*(u_int32_t *)(buf + 0));
	reply =ntohl(*(u_int32_t *)(buf + 4));

	if(size != bytenum) {
		fprintf(stderr, "Error in the size of the reply\n");
		close(*sockfd);
		return 0;
	}

	switch (reply) {
	case 0:
		return 1;
		
	case 1: 
		fprintf(stderr, "Too many connections\n");
		close(*sockfd);
		return 0;

	default:
		fprintf(stderr, "Undefined Error\n");
		close(*sockfd);
		return 0;
	}
#else
	return 1;
#endif
}


/*
 * Disconnects from the Manager
 * (on succesfull disconnect, it also closes the socket)
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0________4________8
 * |  size  | packet |
 * |________|__type__|
 *
 */
int manager_disconnect(int sockfd)
{
#ifndef TWO_TIER_ARCH
	unsigned int msglen;
	char buf[8];

	/* Fill the packet buffer with data in Network Byte Order */
	msglen = 8;
	*(u_int32_t *)(buf + 0) = htonl(msglen);
	*(u_int32_t *)(buf + 4) = htonl(PT_CLOSE);

	/* Send the data */
	if (sendall(sockfd, buf, msglen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		return 0;
	}

	close(sockfd);
#endif
	return 1;
}

/*
 * Submit a new connection of stream data to the Manager
 * Also fills stream_id with a pointer to a newly allocated stream_id
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0________4________8________12_______________________24
 * |  size  | packet | stream |     struct tuple4      |
 * |________|__type__|___ID___|________________________|
 *
 */
int new_stream_connection(int sockfd, const struct tuple4 *tcp_addr,
			  unsigned int **stream_id, enum data_t type)
{
#ifndef TWO_TIER_ARCH
	unsigned int msglen;
	char buf[24];

	if ((*stream_id = init_stream()) == NULL) {
		fprintf(stderr, "Could not get new stream id\n");
		return 0;
	}

	/* Fill the packet buffer with data in Network Byte Order */
	msglen = 24;
	*(u_int32_t *)(buf +  0) = htonl(msglen);
	*(u_int32_t *)(buf +  4) = htonl(PT_NEW_TCP);
	*(u_int32_t *)(buf +  8) = htonl(**stream_id);
	*(u_int16_t *)(buf + 12) = htons(tcp_addr->source);
	*(u_int16_t *)(buf + 14) = htons(tcp_addr->dest);
	*(u_int32_t *)(buf + 16) = tcp_addr->saddr;
	*(u_int32_t *)(buf + 20) = tcp_addr->daddr;

	/* Send the Data */
	if (sendall(sockfd, buf, msglen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		return 0;
	}

	return 1;
#else /* TWO_TIER_ARCH */

	if ((*stream_id = init_stream()) == NULL) {
		fprintf(stderr, "Could not get new stream id\n");
		return 0;
	}

	return 	new_tcp(**stream_id, tcp_addr);
#endif
}


/*
 * Informs the Manager that a stream connection has closed
 * and destroys the associated stream_id.
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0________4________8________12
 * |  size  | packet | stream |
 * |________|__type__|___ID___|
 *
 */
int close_stream_connection(int sockfd, unsigned *stream_id)
{
#ifndef TWO_TIER_ARCH
	unsigned int msglen;
	char buf[12];


	/* Fill the packet buffer with data in Network Byte Order */
	msglen = 12;
	*(u_int32_t *)(buf + 0) = htonl(msglen);
	*(u_int32_t *)(buf + 4) = htonl(PT_TCP_CLOSE);
	*(u_int32_t *)(buf + 8) = htonl(*stream_id);

	/* Send the Data */
	if (sendall(sockfd, buf, msglen) == -1) {
		perror("sendall");
		return 0;
	}
#else /* TWO_TIER_ARCH */
	close_tcp(*stream_id);
#endif
	free(stream_id);

	return 1;
}


/*
 * Sends stream data to the Manager
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0________4________8________12_______ _ _ _ _
 * |  size  | packet | stream |   data
 * |________|__type__|___ID___|________ _ _ _ _
 *
 */

int send_stream_data(int sockfd, unsigned stream_id, const void *data, 
		     size_t datalen)
{
	unsigned int msglen;
	unsigned int hdrlen;
	char buf[12];

	if (datalen <= 0) {
		/*this is an error....*/
		fprintf(stderr,"send_tcp_data: No Data to send...\n");
		return 0;
	}

	hdrlen = 12;
	msglen = hdrlen + datalen; /* header + data */
	*(u_int32_t *)(buf + 0) = htonl(msglen);
	*(u_int32_t *)(buf + 4) = htonl(PT_TCP_DATA);
	*(u_int32_t *)(buf + 8) = htonl(stream_id);

	/* first send the header */
	if (sendall(sockfd, buf, hdrlen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		return 0;
	}

	/* then send the data... */
	if (sendall(sockfd, data, datalen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		return 0;
	}

	return 1;
}

/*
 * Informs the Manager that the next stream data belong to a new group
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0________4________8________12
 * |  size  | packet | stream |
 * |________|__type__|___ID___|
 *
 */

int break_stream_data(int sockfd, unsigned int stream_id)
{
#ifndef TWO_TIER_ARCH
	unsigned int msglen;
	char buf[12];


	/* Fill the packet buffer with data in Network Byte Order */
	msglen = 12;
	*(u_int32_t *)(buf + 0) = htonl(msglen);
	*(u_int32_t *)(buf + 4) = htonl(PT_TCP_BREAK);
	*(u_int32_t *)(buf + 8) = htonl(stream_id);

	/* Send the Data */
	if (sendall(sockfd, buf, msglen) == -1) {
		perror("sendall");
		return 0;
	}

	return 1;
#else /* TWO_TIER_ARCH */
	return tcp_break(stream_id);
#endif
}


/*
 * Sends datagram data to the Manager
 * returns 1 on success, 0 on failure
 *
 * packet structure:
 * 0_______4_______8______12________________24_______ _ _ _ _
 * | packet| packet|  ID   |   struct tuple4 |  data
 * |__size_|__type_|_______|_________________|_______ _ _ _ _
 *
 */

int send_dgram_data(int sockfd, const struct tuple4 *udp_addr, 
		    const void *data, size_t datalen, enum data_t type)
{
	unsigned int msglen;
	unsigned int hdrlen;
	unsigned int id;
	char buf[24];

	if (datalen <= 0) {
		fprintf(stderr, "send_udp_data: No Data to send...\n");
		return 0;
	}

	id = get_new_id();

	/* The header first... */
	hdrlen = 24;
	msglen = hdrlen + datalen;
	*(u_int32_t *)(buf +  0) = htonl(msglen);
	*(u_int32_t *)(buf +  4) = htonl(PT_UDP_DATA);
	*(u_int32_t *)(buf +  8) = htonl(id);
	*(u_int16_t *)(buf + 12) = htons(udp_addr->source);
	*(u_int16_t *)(buf + 14) = htons(udp_addr->dest);
	*(u_int32_t *)(buf + 16) = udp_addr->saddr;
	*(u_int32_t *)(buf + 20) = udp_addr->daddr;



	if (sendall(sockfd, buf, hdrlen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		return 0;
	}

	/* the data... */
	if (sendall(sockfd, data, datalen) == -1) {
		fprintf(stderr,"Error in sendall\n");
		return 0;
	}

	return 1;
}
