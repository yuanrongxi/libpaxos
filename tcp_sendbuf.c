#include "tcp_sendbuf.h"
#include <event2/bufferevent.h>

/*构建一个paxos msg头信息，并发送到网络*/
static void add_paxos_header(struct bufferevent* bev, paxos_msg_code c, size_t s)
{
	paxos_msg m;
	m.data_size = s;
	m.type = c;
	bufferevent_write(bev, &m, sizeof(paxos_msg));
}

void sendbuf_add_prepare_req(struct bufferevent* bev, prepare_req* pr)
{
	size_t s = PREPARE_REQ_SIZE(pr);
	add_paxos_header(bev, prepare_reqs, s);
	bufferevent_write(bev, pr, s);
	paxos_log_debug("Send prepare iid: %d ballot: %d", pr->iid, pr->ballot);
}

void sendbuf_add_prepare_ack(struct bufferevent* bev, acceptor_record* rec)
{
	size_t s;
	prepare_ack pa;

	pa.accept_id = rec->acceptor_id;
	pa.ballot = rec->ballot;
	pa.iid = rec->iid;
	pa.value_ballot = rec->value_ballot;
	pa.value_size = rec->value_size;

	s = PREPARE_ACK_SIZE((&pa));
	add_paxos_header(bev, prepare_acks, s);
	bufferevent_write(bev, &pa, sizeof(prepare_ack));
	if(pa.value_size > 0){
		bufferevent_write(bev, rec->value, rec->value_size);
	}

	paxos_log_debug("Send prepare ack for inst %d ballot %d", rec->iid, rec->ballot);
}

void sendbuf_add_accept_req(struct bufferevent* bev, accept_req* ar)
{
	size_t s = ACCEPT_REQ_SIZE(ar);
	add_paxos_header(bev, accept_reqs, s);
	bufferevent_write(bev, ar, s);
	paxos_log_debug("Send accept req for inst %d ballot %d", ar->iid, ar->ballot);
}

void sendbuf_add_accept_ack(struct bufferevent* bev, acceptor_record* rec)
{
	size_t s = ACCEPT_ACK_SIZE(rec);
	add_paxos_header(bev, accept_acks, s);
	bufferevent_write(bev, rec, s);
	paxos_log_debug("Send accept ack for inst %d ballot %d", aa->iid, aa->ballot);
}

void sendbuf_add_repeat_req(struct bufferevent* bev, iid_t iid)
{
	add_paxos_header(bev, repeat_reqs, sizeof(iid_t));
	bufferevent_write(bev, &iid, sizeof(iid_t));
	paxos_log_debug("Send repeat request for inst %d", iid);
}

void paxos_submit(struct bufferevent* bev, char* value, int size)
{
	add_paxos_header(bev, submit, size);
	bufferevent_write(bev, value, size);
}

