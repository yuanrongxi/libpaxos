#ifndef __CONFIG_READER_H_
#define __CONFIG_READER_H_

#include "evpaxos.h"

struct evpaxos_config;

struct evpaxos_config*	evpaxos_config_read(const char* path);
void					evpaxos_config_free(struct evpaxos_config* config);

int						evpaxos_proposer_count(struct evpaxos_config* c);
int						evpaxos_acceptor_count(struct evpaxos_config* c);

struct sockaddr_in		evpaxos_proposer_address(struct evpaxos_config* c, int i);
int						evpaxos_proposer_listen_port(struct evpaxos_config* c, int i);
struct sockaddr_in		evpaxos_acceptor_address(struct evpaxos_config* c, int i);
int						evpaxos_acceptor_listen_port(struct evpaxos_config* c, int i);

#endif

