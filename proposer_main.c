#include <stdlib.h>
#include <stdio.h>
#include <evpaxos.h>
#include <signal.h>

void handle_sigint(int sig, short ev, void* arg)
{
	struct event_base* base = arg;
	printf("Caught signal %d\n", sig);
	event_base_loopexit(base, NULL);
}

int main (int argc, char const *argv[])
{
	struct event* sig;
	struct event_base* base;
	struct evproposer* prop;

	if (argc != 3) {
		printf("Usage: %s id config\n", argv[0]);
		exit(1);
	}

	base = event_base_new();
	sig = evsignal_new(base, SIGINT, handle_sigint, base);
	evsignal_add(sig, NULL);

	/*参数为id和配置文件路径*/
	prop = evproposer_init(atoi(argv[1]), argv[2], base);
	if (prop == NULL) {
		printf("Could not start the proposer!\n");
		exit(1);
	}

	event_base_dispatch(base);

	event_free(sig);
	evproposer_free(prop);
	event_base_free(base);

	return 0;
}