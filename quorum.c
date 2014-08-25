#include "paxos.h"
#include "quorum.h"
#include <stdlib.h>
#include <string.h>

/*构建一个通过决议的acceptor id标示，其实可以考虑用bitmap实现*/
void quorum_init(struct quorum *q, int acceptors)
{
	q->acceptors = acceptors;
	q->quorum = paxos_quorum(acceptors);/*acceptors数量的一半 + 1*/
	q->acceptor_ids = (int*)malloc(sizeof(int) * q->acceptors);

	quorum_clear(q);
}

void quorum_clear(struct quorum* q)
{
	q->count = 0;
	memset(q->acceptor_ids, 0, q->acceptors * sizeof(int));
}

void quorum_destroy(struct quorum* q)
{
	q->acceptors = 0;
	free(q->acceptor_ids);
}

/*增加一个acceptor通过标识*/
int quorum_add(struct quorum* q, int id)
{
	if(q->acceptor_ids[id] == 0){
		q->count ++;
		q->acceptor_ids[id] = 1;

		return 1;
	}

	return 0;
}

int quorum_reached(struct quorum* q)
{
	return (q->count >= q->quorum); /*判断是否大多数通过*/
}

