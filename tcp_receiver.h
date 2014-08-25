#ifndef __TCP_RECEIVER_H
#define __TCP_RECEIVER_H

#include "evpaxos.h"
#include "carray.h"
#include "config.h"
#include <event2/event.h>
#include <event2/bufferevent.h>

struct tcp_receiver
{
	bufferevent_data_cb callback;
	void* arg;
	struct evconnlistener* listener;
	struct carray* bevs;
};
/*创建一个tcp receiver*/
struct tcp_receiver* tcp_receiver_new(struct event_base* b, int port, bufferevent_data_cb cb, void* arg);
/*销毁一个tcp recevier*/
void tcp_receiver_free(struct tcp_receiver* r);
/*获取tcp recevier的事件*/
struct carray* tcp_receiver_get_events(struct tcp_recevier* r);

#endif

