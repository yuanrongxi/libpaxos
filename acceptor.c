#include "acceptor.h"
#include "storage.h"

#include <stdlib.h>

struct acceptor
{
	struct storage* store;
};

static acceptor_record* apply_prepare(struct storage* s, prepare_req* ar, acceptor_record* rec);
static acceptor_record* apply_accept(struct storage* s, accept_req* ar, acceptor_record* rec);

struct acceptor* acceptor_new(int id)
{
	struct acceptor* s = (struct acceptor *)malloc(sizeof(struct acceptor));
	s->store = storage_open(id);/*打开固化存储系统，acceptor接受的信息应该固化*/
	if(s->store == NULL){
		free(s);
		return NULL;
	}

	return s;
}

int acceptor_free(struct acceptor* a)
{
	int rv = 0;
	if(a && a->store){
		rv = storage_close(a->store);
		free(a);
	}

	return rv;
}

void acceptor_free_record(struct acceptor* a, acceptor_record* r)
{
	if(a && a->store && r)
		storage_free_record(a->store, r);
}

acceptor_record* acceptor_receive_prepare(struct acceptor* a, prepare_req* req)
{
	acceptor_record* rec;
	storage_tx_begin(a->store);
	rec = storage_get_record(a->store, req->iid);
	/*接受prepare req请求*/
	rec = apply_prepare(a->store, req, rec);
	storage_tx_commit(a->store);
	return rec;
}

acceptor_record* acceptor_receive_accept(struct acceptor* a, accept_req* req)
{
	acceptor_record* rec;
	storage_tx_begin(a->store);
	rec = storage_get_record(a->store, req->iid);
	/*接受accept_req请求*/
	rec = apply_accept(a->store, req, rec);
	storage_tx_commit(s->store);

	return rec;
}
acceptor_record* acceptor_receive_repeat(struct acceptor* a, iid_t iid)
{
	acceptor_record* rec;
	storage_tx_begin(a->store);
	rec = storage_get_record(a->store, iid);
	storage_tx_commit(a->store);
	return rec;
}
static acceptor_record* apply_prepare(struct storage* s, prepare_req* pr, acceptor_record* rec)
{
	/*不接受小于本acceptor已接受的最大提议ID的提议，返回最大接受的提议信息*/
	if(rec != NULL && rec->ballot >= pr->ballot){
		paxos_log_debug("Prepare iid: %u dropped (ballots curr:%u recv:%u)", pr->iid, rec->ballot, pr->ballot);
		return rec;
	}

	/*决议内容已经审批通过，不接受新的提议，返回通过的提议信息*/
	if (rec != NULL && rec->is_final) {
		paxos_log_debug("Prepare request for iid: %u dropped (stored value is final)", pr->iid);
		return rec;
	}

	/*接受提议*/
	paxos_log_debug("Preparing iid: %u, ballot: %u", pr->iid, pr->ballot);
	if(rec != NULL)
		storage_free_record(s, rec);

	/*保存一个新的提议信息*/
	return storage_save_prepare(s, pr);
}

static acceptor_record* apply_accept(struct storage* s, accept_req* ar, acceptor_record* rec)
{
	if (rec != NULL && rec->ballot > ar->ballot) { /*相等是可以的，因为是同一个提案*/
		paxos_log_debug("Accept for iid:%u dropped (ballots curr:%u recv:%u)", ar->iid, rec->ballot, ar->ballot);
		return rec;
	}

	paxos_log_debug("Accepting iid: %u, ballot: %u", ar->iid, ar->ballot);

	/*释放旧的record*/
	if (rec != NULL)
		storage_free_record(s, rec);
	/*保存提案内容*/
	return storage_save_accept(s, ar);
}

