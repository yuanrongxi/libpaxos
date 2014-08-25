#ifndef __ACCEPTOR_H_
#define __ACCEPTOR_H_

#include "paxos.h"
#include "libpaxos_message.h"

struct acceptor;

struct acceptor*	acceptor_new(int id);
int					acceptor_free(struct acceptor* a);
void				acceptor_free_record(struct acceptor* a, acceptor_record* r);

acceptor_record*	acceptor_receive_prepare(struct acceptor* a, prepare_req* req);
acceptor_record*	acceptor_receive_accept(struct acceptor* a, accept_req* req);
acceptor_record*	acceptor_receive_repeat(struct acceptor* a, iid_t iid);

#endif
