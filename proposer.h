#ifndef __PROPOSER_H_
#define __PROPOSER_H_

#include "paxos.h"
#include "libpaxos_message.h"

struct proposer;
struct timeout_iterator;

struct proposer*			proposer_new(int id, int acceptors);
void						proposer_free(struct proposer* p);

void						proposer_propose(struct proposer* p, const char* value, size_t size);
int							proposer_prepared_count(struct proposer* p);

/*phase 1*/
void						proposer_prepare(struct proposer* p, prepare_req* out);
int							proposer_receive_prepare_ack(struct proposer* p, prepare_ack* ack, prepare_req* out);

/*phase 2*/
accept_req*					proposer_accept(struct proposer* p);
int							proposer_receive_accept_ack(struct proposer* p, accept_ack* ack, prepare_req* out);

/*timeouts*/
struct timeout_iterator*	proposer_timeout_iterator(struct proposer* p);
prepare_req*				timeout_iterator_prepare(struct timeout_iterator* iter);
accept_req*					timeout_iterator_accept(struct timeout_iterator* iter);
void						timeout_iterator_free(struct timeout_iterator* iter);

#endif
