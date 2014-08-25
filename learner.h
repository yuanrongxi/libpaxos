#ifndef __LEARNER_H_
#define __LEARNER_H_

#include "paxos.h"
#include "libpaxos_message.h"

struct learner;

struct learner* learner_new(int acceptors);
void			learner_free(struct learner* l);
void			learner_receive_accept(struct learner* l, accept_ack* ack);
accept_ack*		learner_deliver_next(struct learner* l);
int				learner_has_holes(struct learner* l, iid_t* f, iid_t* from, iid_t* to);

#endif
