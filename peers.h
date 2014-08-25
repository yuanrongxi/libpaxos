#ifndef __PEERS_H_
#define __PEERS_H_

#include "config.h"
#include <event2/bufferevent.h>

struct peers;

struct peers*		peers_new(struct event_base* base);
void				peers_free(struct peers* p);
void				peers_connect(struct peers* p, struct sockadd_in* addr, bufferevent_data_cb cb, void* arg);
void				peers_connect_to_acceptors(struct peers* p, struct evpaxos_config* conf, bufferevent_data_cb cb, void* arg);
int					peers_count(struct peers* p);
struct bufferevent* peers_get_buffer(struct peers* p, int i);

#endif
