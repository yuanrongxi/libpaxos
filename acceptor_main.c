#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "evpaxos.h"

void handle_sigint(int sig, short ev, void* arg)
{
	struct event_base* base = arg;
	printf("Caught signal %d\n", sig);
	event_base_lookexit(base, NULL);
}

int main(int argc, const char* argv[])
{
	int id;
	struct event* sig;
	struct evacceptor* acc;
	struct event_base* base;

	if(argc != 3){
		printf("Usage %s id config\n", argv[0]);
		return 0;
	}
	/*构建libevent base*/
	base = event_base_new();

	/*读取acceptor id,全局唯一*/
	id = atoi(argv[1]);
	/*创建一个acceptor 的事件响应器*/
	acc = evacceptor_init(id, argv[2], base);
	if(acc == NULL){
		printf("Could not start the acceptor\n");
		return 0;
	}

	sig = evsignal_new(base, SIGINT, handle_sigint, base);
	evsignal_add(sig, NULL);

	event_base_dispatch(base);

	event_free(sig);
	evacceptor_free(acc);
	event_base_free(base);

	return 1;
}



