#ifndef __PAXOS_CARRAY_H
#define __PAXOS_CARRAY_H

#include "paxos.h"
#include "libpaxos_message.h"

struct carray;

struct carray* carray_new(int size);
void carray_free(struct carray* a);
int carray_empty(struct carray* a);
int carray_size(struct carray* a);

int carray_push_back(struct carray* a, void* p);
int carray_push_front(struct carray* a, void* p);
void* carray_front(struct carray* a);
void* carray_pop_front(struct carray* a);

int	carray_count(struct carray* a);
void* carray_at(struct carray* a, int i);

typedef int(*match_fn_t)(void*, void*);

void* carray_first_match(struct carray* a, match_fn_t match_fn, void* arg);
int carray_count_match(struct carray* a, match_fn_t match_fn, void* arg);
struct carray* carray_collect(struct carray* a, match_fn_t match_fn, void* arg);
struct carray* carray_reject(struct carray* a, match_fn_t match_fn, void* arg);

#endif
