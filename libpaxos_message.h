#ifndef __LIB_PAXOS_MESSAGE_H
#define __LIB_PAXOS_MESSAGE_H

#include "paxos.h"
#include <stdlib.h>

typedef enum 
{
	prepare_reqs	= 0x01,
	prepare_acks	= 0x02,
	accept_reqs		= 0x04,
	accept_acks		= 0x08,
	repeat_reqs		= 0x10,
	submit			= 0x20,
	leader_announce = 0x40, /*proposer leader的决议宣布协议，未实现*/
	alive_ping		= 0x41,
} paxos_msg_code;


typedef struct paxos_msg_t
{
	size_t			data_size; /*有可能会32位系统和64位系统是不一样的*/
	paxos_msg_code	type;
	char			data[0];
} __attribute__((packed)) paxos_msg;
#define PAXOS_MSG_SIZE(m)	(m->data_Size + sizeof(paxos_msg))

typedef struct prepare_req_t 
{
	iid_t		iid;
	ballot_t	ballot;
} prepare_req;
#define PREPARE_REQ_SIZE(m) (sizeof(prepare_req_t))

typedef struct prepare_ack_t
{
	int			accept_id;		/*用来判断acceptor的身份，用于大多数判断*/
	iid_t		iid;
	ballot_t	ballot;
	ballot_t	value_ballot;
	size_t		value_size;
	char		value[0];
}prepare_ack;
#define PREPARE_ACK_SIZE(m) (m->value_size + sizeof(prepare_ack))

typedef struct accept_req_t
{
	iid_t		iid;
	ballot_t	ballot;
	size_t		value_size;
	char		value[0];
}accept_req;
#define ACCEPT_REQ_SIZE(m) (m->value_size + sizeof(accept_req))

typedef struct accept_ack_t
{
	int			acceptor_id;
	iid_t		iid;
	ballot_t	ballot;
	ballot_t	value_ballot;
	short int	is_final;
	size_t		value_size;
	char		value[0];
}accept_ack;
#define ACCEPT_ACK_SIZE(m) (m->value_size + sizeof(accept_ack))

typedef accept_ack	acceptor_record;
#define ACCEPT_RECORD_BUFF_SIZE(value_size) (value_size + sizeof(accept_ack))

#endif
