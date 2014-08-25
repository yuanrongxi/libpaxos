#include "evpaxos.h"
#include "peers.h"
#include "config.h"
#include "libpaxos_message.h"
#include "tcp_sendbuf.h"
#include "tcp_receiver.h"
#include "proposer.h"

#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

struct evproposer
{
	int						id;				/*proposer id*/
	int						preexec_window;	/*同时可以发起提议的个数,仅限第一阶段*/
	struct tcp_receiver*	receiver;		/*tcp消息接受器*/
	struct event_base*		base;			/*libevent base*/
	struct proposer*		state;			/*proposer 消息处理器*/
	struct peers*			acceptors;		/*acceptor连接节点管理器*/
	struct timval			tv;				/*定时时间*/
	struct event*			timeout_ev;		/*定时时间句柄*/
};

/*发送prepare_req到所有的acceptor进行第一阶段的提议*/
static void send_prepares(struct evproposer* p, prepare_req* pr)
{
	int i;
	for(i = 0; i < peers_count(p->acceptors); i ++){
		struct bufferevent* bev = peers_get_buffer(p->acceptors, i);
		sendbuf_add_prepare_req(bev, pr);
	}
}

/*发送accept_req到所有的acceptor进行第二阶段的提议*/
static void send_accepts(struct evproposer* p, accept_req* ar)
{
	int i;
	for(i = 0; i < peers_count(p->acceptors); i++){
		struct bufferevent* bev = peers_get_buffer(p->acceptors, i);
		sendbuf_add_accept_req(bev, ar);
	}
}

static void proposer_preexecute(struct evproposer* p)
{
	int i;
	prepare_req pr;

	/*获得可以发起提案的个数*/
	int count = p->preexec_window - proposer_prepared_count(p->state);
	for(i = 0; i < count; i ++){
		 /*构建一个prepare_req消息*/
		proposer_prepare(p->state, &pr);
		/*发起一个提案*/
		send_prepares(p, &pr);
	}
}

static void try_accept(struct evproposer* p)
{
	accept_req* ar;
	while((ar = proposer_accept(p->state)) != NULL){ /*获取一个完成了第一阶段的提议，并根据其构建一个accept_reqs*/
		/*发起提议的第二阶段*/
		send_accepts(p, ar);
		free(ar);
	}

	/*检查是否可以发送更多的第一阶段的提议*/
	proposer_preexecute(p);
}

/*proposer对prepare ack的处理和响应*/
static void proposer_handle_prepare_ack(struct evproposer* p, prepare_ack* ack)
{
	prepare_req pr;
	if(proposer_receive_prepare_ack(p->state, ack, &pr)) /*对prepare ack的响应处理，判断返回值是否是需要重新发起第一阶段*/
		send_prepares(p, &pr);
}

/*proposer对accept ack的处理和响应*/
static void proposer_handle_accept_ack(struct evproposer* p, accept_ack* ack)
{
	prepare_req pr;
	if (proposer_receive_accept_ack(p->state, ack, &pr))/*对accept ack的响应处理，判断返回值是否是需要重新发起第一阶段*/
		send_prepares(p, &pr);
}

/*接受来自客户端的消息，将消息转化为等待提议的内容*/
static void proposer_handle_client_msg(struct evproposer* p, char* value, int size)
{
	proposer_propose(p->state, value, size);
}

/*proposer处理网络消息接口*/
static void proposer_handle_msg(struct evproposer* p, struct bufferevent* bev)
{
	paxos_msg msg;
	struct evbuffer* in;
	char* buffer = NULL;

	/*解读消息头*/
	in = bufferevent_get_input(bev);
	evbuffer_remove(in, &msg, sizeof(paxos_msg));

	/*解读消息体*/
	if (msg.data_size > 0) {
		buffer = malloc(msg.data_size);
		evbuffer_remove(in, buffer, msg.data_size);
	}

	/*处理消息*/
	switch (msg.type){
	case prepare_acks:
		proposer_handle_prepare_ack(p, (prepare_ack*)buffer);
		break;
	case accept_acks:
		proposer_handle_accept_ack(p, (accept_ack*)buffer);
		break;
	case submit:
		proposer_handle_client_msg(p, buffer, msg.data_size);
		break;
	default:
		paxos_log_error("Unknow msg type %d not handled", msg.type);
		return;
	}

	/*尝试发起提议的第二阶段,阶段性检查*/
	try_accept(p);

	if (buffer != NULL)
		free(buffer);
}

static void handle_request(struct bufferevent* bev, void* arg)
{
	size_t len;
	paxos_msg msg;
	struct evproposer* p = (struct evproposer*)arg;

	struct evbuffer* in = bufferevent_get_input(bev);

	/*读取触发的消息，循环读取，防止粘包*/
	while ((len = evbuffer_get_length(in)) > sizeof(paxos_msg)){
		evbuffer_copyout(in, &msg, sizeof(paxos_msg));
		
		if(len >= PAXOS_MSG_SIZE((&msg))){
			proposer_handle_msg(p, bev);
		}
	}
}

/*检查超时的提案，进行重新提议*/
static void proposer_check_timeouts(evutil_socket_t fd, short event, void* arg)
{
	struct evproposer* p = arg;
	struct timeout_iterator* iter = proposer_timeout_iterator(p->state);

	/*第一个阶段超时提案*/
	prepare_req* pr;
	while((pr == timeout_iterator_prepare(iter)) != NULL){ /*获取超时的提案(第一阶段)*/
		paxos_log_info("Instance %d timed out.", pr->iid);
		/*对超时提案重新发起提议过程*/
		send_prepares(p, pr);
		free(pr);
	}
	
	accept_req* ar;
	while((ar = timeout_iterator_accept(iter)) != NULL){ /*获得超时提案(第二阶段)*/
		paxos_log_info("Instance %d timed out.", ar->iid);
		send_accepts(p, ar);
		free(ar);
	}

	/*释放超时管理的迭代器*/
	timeout_iterator_free(iter);
	/*插入一个定时器*/
	event_add(p->timeout_ev, &p->tv);
}

/*建立一个proposer对象，并启动它*/
struct evproposer* evproposer_init(int id, const char* config, struct event_base* b)
{
	int port, acceptor_count;
	struct evproposer* p;

	/*读取配置文件*/
	struct evpaxos_config* conf = evpaxos_config_read(config);
	if(conf == NULL)
		return NULL;

	/*非法的proposer id*/
	if (id < 0 || id >= MAX_N_OF_PROPOSERS) {
		paxos_log_error("Invalid proposer id: %d", id);
		return NULL;
	}

	/*读取proposer的监听端口*/
	port = evpaxos_proposer_listen_port(conf, id);
	/*读取acceptor的数量*/
	acceptor_count = evpaxos_acceptor_count(conf);

	p = (struct evproposer *)malloc(sizeof(struct evproposer));
	p->id = id;
	p->base = b;

	/*获得同时提交的议案数量*/
	p->preexec_window = paxos_config.proposer_preexec_window;
	
	/*产生一个网络消息接收器*/
	p->receiver = tcp_receiver_new(b, port, handle_request, p);
	
	/*产生一个acceptor的管理器*/
	p->acceptors = peers_new(b);
	/*对每个acceptor发起连接*/
	peers_connect_to_acceptors(p->acceptors, conf, handle_request, p);
	
	/*设置定时器*/
	p->tv.tv_sec = paxos_config.proposer_timeout;
	p->tv.tv_usec = 0;
	/*产生一个libevent定时器事件对象,并设置一个定时器*/
	p->timeout_ev = evtimer_new(b, proposer_check_timeouts, p);
	event_add(p->timeout_ev, &p->tv);

	/*产生一个proposer 消息处理器*/
	p->state = proposer_new(p->id, acceptor_count);

	/*试探性执行prepare过程(提案第一阶段)*/
	proposer_preexecute(p);

	evpaxos_config_free(conf);

	return p;
}

/*释放evproposer对象*/
void proposer_free(struct evproposer* p)
{
	if(p != NULL){
		if(p->state != NULL)
			proposer_free(p->state);

		if(p->acceptors != NULL)
			peers_free(p->acceptors);

		if(p->receiver != NULL)
			tcp_receiver_free(p->receiver);

		free(p);
	}
}







