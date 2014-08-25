#include "evpaxos.h"
#include "learner.h"
#include "peers.h"
#include "tcp_sendbuf.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>


struct evlearner
{
	struct learner*			state;		/*learner的消息处理器*/	
	deliver_function*		delfun;		/*提案发表call back*/
	void*					delarg;		/*defun回调参数*/
	struct event*			hole_timer;	/*检查holes的定时器*/
	struct timeval			tv;			/*定时器时间*/
	struct peers*			acceptors;	/*到acceptors的连接管理器*/
};

#define LEARNER_CHUNNK 10000

static void learner_check_holes(evutil_socket_t fd, short event, void* arg)
{
	int i;
	iid_t iid, from, to;
	struct evlearner* l = (struct evlearner*)arg;

	/*检查holes,看是否有等待完成的提案*/
	if(learner_has_holes(l->state, &from, &to)){
		if(to - from > LEARNER_CHUNNK)
			to = from + LEARNER_CHUNNK;

		for(iid = from; iid < to; iid ++){
			for(i = 0; i < peers_count(l->acceptors); i ++){ /*重新再请求acceptor发送决议结果*/
				struct bufferevent* bev = peers_get_buffer(l->acceptors, i);
				sendbuf_add_repeat_req(bev, iid);
			}
		}
	}

	/*插入定时器*/
	event_add(l->hole_timer, &l->tv);
}

static void learner_deliver_next_closed(struct evlearner* l)
{
	int prop_id;
	accept_ack* ack;
	while((ack = learner_deliver_next(l->state)) != NULL){
		/*计算提议的proposer id*/
		prop_id = ack->ballot % MAX_N_OF_PROPOSERS;
		l->delfun(ack->value, ack->value_size, ack->iid, ack->ballot, prop_id, l->delarg);

		free(ack);
	}
}

static void learner_handle_accept_ack(struct evlearner* l, accept_ack * aa)
{
	/*进行accept ack处理*/
	learner_receive_accept(l->state, aa);

	/*检查是否有可以发表的提案*/
	learner_deliver_next_closed(l);
}

static void learner_handle_msg(struct evlearn* l, struct bufferevent* bev)
{
	paxos_msg msg;
	struct evbuffer* in;
	char* buffer = NULL;

	/*接受消息头*/
	in = bufferevent_get_input(bev);
	evbuffer_remove(in, &msg, sizeof(paxos_msg));

	/*接受消息体*/
	if(msg.data_size > 0){
		buffer = (char *)malloc(msg.data_size);
		evbuffer_remove(in, buffer, msg.data_size);
	}

	switch(msg.type){
	case accept_acks: 
		learner_handle_accept_ack(l, (accept_ack*)buffer);
		break;

	default:
		paxos_log_error("Unknow msg type %d not handled", msg.type);
	}

	if(buffer != NULL)
		free(buffer);
}

static void on_acceptor_msg(struct bufferevent* bev, void* arg)
{
	size_t len;
	paxos_msg msg;
	struct evlearner* l = arg;
	struct evbuffer* in = bufferevent_get_input(bev);

	/*对消息进行循环接受*/
	while ((len = evbuffer_get_length(in)) > sizeof(paxos_msg)) {
		evbuffer_copyout(in, &msg, sizeof(paxos_msg));
		if (len >= PAXOS_MSG_SIZE((&msg))) /*检查消息长度*/
			learner_handle_msg(l, bev); /*进行消息处理*/
	}
}

/*根据配置文件信息构建一个evlearner对象*/
static struct evlearner* evlearner_init_conf(struct evpaxos_config* c, deliver_function f, void* arg, struct event_base* b)
{
	struct evlearner* l;
	/*获取acceptor的个数*/
	int acceptor_count = evpaxos_acceptor_count(c);

	l = (struct evlearner*)malloc(sizeof(struct evlearner*));
	l->delfun = f;
	l->delarg = arg;
	l->state = learner_new(acceptor_count);
	/*构建一个acceptor连接管理*/
	l->acceptors = peers_new(b);
	/*发起对acceptor的连接*/
	peers_connect_to_acceptors(l->acceptors, c, on_acceptor_msg, l);

	l->tv.tv_sec = 0;
	l->tv.tv_usec = 100000; /*100ms*/
	/*设置检查holes的定时器事件*/
	l->hole_timer = evtimer_new(b, learner_check_holes, l);
	/*添加一个定时事件*/
	event_add(l->hole_timer, &l->tv);

	return l;
}

struct evlearner* evlearner_init(const char* config_file, deliver_function f, void* arg, struct event_base* base)
{
	/*读取配置文件*/
	struct evpaxos_config* c = evpaxos_config_read(config_file);
	if(c) /*根据配置文件*/
		return evlearner_init_conf(c, f, arg, base);

	return NULL;
}

void evlearner_free(struct evlearner* l)
{
	/*释放连接管理器*/
	peers_free(l->acceptors);
	/*释放检查hole的定时器*/
	event_free(l->hole_timer);
	/*释放消息处理器*/
	learner_free(l->state);

	free(l);
}





