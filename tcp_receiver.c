#include "tcp_receiver.h"
#include "libpaxos_message.h"

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <event2/listener.h>
#include <event2/buffer.h>

static void set_sockaddr_in(struct sockaddr_in* sin, int port)
{
	memset(sin, 0, sizeof(struct sockaddr_in));

	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(0);
	sin->sin_port = htons(port);
}

static void on_read(struct bufferevent* bev, void* arg)
{
	size_t len;
	paxos_msg msg;
	struct evbuffer* in;
	struct tcp_receiver* r = (struct tcp_receiver *)arg;

	/*读取paxos msg，可能是多个*/
	in = bufferevent_get_input(bev);

	while((len = evbuffer_get_length(in)) > sizeof(paxos_msg)){
		evbuffer_copyout(in, &msg, sizeof(paxos_msg));
		if (len < PAXOS_MSG_SIZE((&msg)))
			return;

		r->callback(bev, r->arg);
	}
}

static int match_bufferevent(void* arg, void* item)
{
	return arg == item;
}

static void on_error(struct bufferevent *bev, short events, void* arg)
{
	struct tcp_receiver* r = (struct tcp_receiver *)arg;
	if(events & (BEV_EVENT_EOF)){
		struct carray* tmp = carray_reject(r->bevs, match_bufferevent, bev); /*过滤掉bev的事件*/
		carray_free(r->bevs);
		r->bevs = tmp;
		bufferevent_free(bev);
	}
}

static void on_accept(struct evconnlistener* l, evutil_socket_t* fd, struct sockaddr* addr, int socklen, void *arg)
{
	struct tcp_receiver* r = arg;
	struct event_base* b = evconnlistener_get_base(l);
	struct bufferevent *bev = bufferevent_socket_new(b, fd, BEV_OPT_CLOSE_ON_FREE);
	/*设置读事件函数和错误函数*/
	bufferevent_setcb(bev, on_read, NULL, on_error, arg);
	/*设置监视的socket事件*/
	bufferevent_enable(bev, EV_READ|EV_WRITE);
	/*添加到事件管理器中*/
	carray_push_back(r->bevs, bev);

	paxos_log_info("Accepted connection from %s:%d", inet_ntoa(((struct sockaddr_in*)addr)->sin_addr), ntohs(((struct sockaddr_in*)addr)->sin_port));
}

static void on_listener_error(struct evconnlistener* l, void* arg)
{
	int err = EVUTIL_SOCKET_ERROR();
	struct event_base *base = evconnlistener_get_base(l);

	paxos_log_error("Listener error %d: %s. Shutting down event loop.", err, evutil_socket_error_to_string(err));
	event_base_loopexit(base, NULL);
}

struct tcp_receiver* tcp_receiver_new(struct event_base* b, int port, bufferevent_data_cb cb, void* arg)
{
	struct tcp_receiver* r;
	struct sockaddr_in sin;
	unsigned flags = LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE;

	r = malloc(sizeof(struct tcp_receiver));
	set_sockaddr_in(&sin, port);
	r->callback = cb;
	r->arg = arg;
	/*libevent listener和bind端口*/
	r->listener = evconnlistener_new_bind(b, on_accept, r, flags,	-1, (struct sockaddr*)&sin, sizeof(sin));
	assert(r->listener != NULL);
	/*设置listen error 回调处理*/
	evconnlistener_set_error_cb(r->listener, on_listener_error);

	r->bevs = carray_new(10);

	paxos_log_info("Listening on port %d", port);

	return r;
}
void tcp_receiver_free(struct tcp_receiver* r)
{
	int i;
	/*释放监视的事件*/
	for (i = 0; i < carray_count(r->bevs); ++i)
		bufferevent_free(carray_at(r->bevs, i));
	/*释放监听*/
	evconnlistener_free(r->listener);

	carray_free(r->bevs);
	free(r);
}

struct carray* tcp_receiver_get_events(struct tcp_receiver* r)
{
	return r->bevs;
}
