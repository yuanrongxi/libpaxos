#ifndef __EVPAXOS_H_
#define __EVPAXOS_H_

#include "paxos.h"

#include <sys/types.h>
#include <stdint.h>
#include <event2/event.h>
#include <event2/bufferevent.h>

struct evlearner;
struct evacceptor;
struct evproposer;


/* When starting a learner you must pass a callback to be invoked whenever a value has been learned.*/
typedef void(*deliver_function)(char*, size_t, iid_t, ballot_t, int, void*);

struct evlearner*	evlearner_init(const char* config_file, deliver_function f, void* arg, struct event_base* base);
void				evlearner_free(struct evlearner* l);

struct evacceptor*  evacceptor_init(int id, const char* config, struct event_base* b);
int					evacceptor_free(struct evacceptor* a);

struct evproposer*	evproposer_init(int id, const char* config, struct event_base* b);
void				evproposer_free(struct evproposer* p);

void				paxos_submit(struct bufferevent* bev, char* value, int size);
#endif
