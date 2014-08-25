#ifndef __QUORUM_H
#define __QUORUM_H

struct quorum
{
	int		count;
	int		quorum;
	int		acceptors;
	int*	acceptor_ids;
};

void	quorum_init(struct quorum *q, int acceptors);
void	quorum_clear(struct quorum* q);
void	quorum_destroy(struct quorum* q);
int		quorum_add(struct quorum* q, int id);
int		quorum_reached(struct quorum* q);

#endif
