#include "proposer.h"
#include "carray.h"
#include "quorum.h"
#include "khash.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>


struct instance
{
	iid_t				iid;
	ballot_t			ballot;
	ballot_t			value_ballot;
	paxos_msg*			value;
	struct quorum		quorum;
	struct timeval		created_at;
};

KHASH_MAP_INIT_INT(instance, struct instance*);

struct proposer
{
	int					id;
	int					acceptors;
	struct carray*		values;
	iid_t				next_prepare_iid;
	khash_t(instance)*  prepare_instances;
	khash_t(instance)*  accept_instances;
};

struct timeout_iterator
{
	khiter_t			pi, ai;
	struct timeval		timeout;
	struct proposer*	proposer;
};

static ballot_t			proposer_next_ballot(struct proposer* p, ballot_t b);
static void				proposer_preempt(struct proposer* p, struct instance* inst, prepare_req* out);
static void				proposer_move_instance(struct proposer* p, khash_t(instance)* f, 	khash_t(instance)* t, struct instance* inst);

static struct instance* instance_new(iid_t iid, ballot_t ballot, int acceptors);

static void				instance_free(struct instance* inst);
static int				instance_has_timedout(struct instance* inst, struct timeval* now);

static accept_req*		instance_to_accept_req(struct instance* inst);
static paxos_msg*		wrap_value(const char* value, size_t size);

struct proposer* proposer_new(int id, int acceptors)
{
	struct proposer* p = malloc(sizeof(struct proposer));
	p->id = id;
	p->acceptors = acceptors;
	p->next_prepare_iid = 0;
	p->values = carray_new(128);
	p->prepare_instances = kh_init(instance);
	p->accept_instances = kh_init(instance);
}

void proposer_free(struct proposer* p)
{
	int i;
	if(p){
		struct instance* inst;
		kh_foreach_value(p->prepare_instances, inst, instance_free(inst));
		kh_foreach_value(p->accept_instances, inst, instance_free(inst));
		kh_destroy(instance, p->prepare_instances);
		kh_destroy(instance, p->accept_instances);

		for(i = 0; i < carray_count(p->values); i++){
			free(carray_at(p->values, i));
		}

		carray_free(p->values);

		free(p);
	}
}

void proposer_propose(struct proposer* p, const char* value, size_t size)
{
	paxos_msg* msg;

	msg = wrap_value(value, size);
	carray_push_back(p->values, msg);
}

int proposer_prepared_count(struct proposer* p)
{
	return kh_size(p->prepare_instances);
}

/*保存发起的提案信息状态，并构建一个prepare req*/
void proposer_prepare(struct proposer* p, prepare_req* out)
{
	int rv;
	iid_t iid = ++(p->next_prepare_iid);
	ballot_t bal = proposer_next_ballot(p, NULL);
	struct instance* inst = instance_new(iid, bal, p->acceptors);
	khiter_t k = kh_put_instance(p->prepare_instances, iid, &rv);
	assert(rv > 0);
	kh_value(p->prepare_instances, k) = inst;
	*out = (prepare_req) {inst->iid, inst->ballot};
}

/*处理prepare ack消息*/
int proposer_receive_prepare_ack(struct proposer* p, prepare_ack* ack, prepare_req* out)
{
	khiter_t k = kh_get_instance(p->prepare_instances, ack->iid);
	if(k == kh_end(p->prepare_instances)){/*不存在的提议请求*/
		paxos_log_debug("Promise dropped, instance %u not pending", ack->iid);
		return 0;
	}

	struct instance* inst = kh_value(p->prepare_instances, k);
	if(ack->ballot < inst->ballot){ /*acceptor 接受的提案号比proposer最近的提案小*/
		paxos_log_debug("Promise dropped, too old");
		return 0;
	}

	if(ack->ballot > inst->ballot){ /*acceptor 接受的提案大于proposer最近的提案*/
		paxos_log_debug("Instance %u preempted: ballot %d ack ballot %d", inst->iid, inst->ballot, ack->ballot);
		proposer_preempt(p, inst, out); /*发起一个比ack->ballot更大的提案号*/
		return 1;
	}

	/*相等的情况，进行大多数统计*/
	if(!quorum_add(&inst->quorum, ack->accept_id)){
		paxos_log_debug("Duplicate promise dropped from: %d, iid: %u", ack->accept_id, inst->iid);
		return 0;
	}

	paxos_log_debug("Received valid promise from: %d, iid: %u", ack->accept_id, inst->iid);

	/*提案通过了，用ack->value(acceptor通过的最大提案号的值)进行value修正*/
	if (ack->value_size > 0) {
		paxos_log_debug("Promise has value");

		if(inst->value == NULL) {
			inst->value_ballot = ack->value_ballot;
			inst->value = wrap_value(ack->value, ack->value_size);
		} else if (ack->value_ballot > inst->value_ballot) {
			free(inst->value);
			inst->value_ballot = ack->value_ballot;
			inst->value = wrap_value(ack->value, ack->value_size);
			paxos_log_debug("Value in promise saved, removed older value");
		} else
			paxos_log_debug("Value in promise ignored");
	}

	return 0;
}

accept_req* proposer_accept(struct proposer* p)
{
	 khiter_t k;
	 struct instance* inst = NULL;
	 khash_t(instance)* h = p->prepare_instances;

	 for(k = kh_begin(h); k != kh_end(h); ++ k){
		 if(!(kh_exist(h, k)))
			 continue;
		 else if(inst == NULL || (inst->iid > kh_value(h, k)->iid)){
			 inst = kh_value(h, k);
		 }
	 }

	 /*没有接受记录或者accept没有过半，不进行第二阶段*/
	 if(inst == NULL || !quorum_reached(&inst->quorum))
		 return NULL;

	 paxos_log_debug("Trying to accept iid %u", inst->iid);

	 if(inst->value == NULL){
		 inst->value = carray_pop_front(p->values);  /*拿出一个值作为提议内容，进入第二阶段*/
		 if(inst->value == NULL){ /*无值可进行提议，直接取消提议*/
			 paxos_log_debug("No value to accept");
			 return NULL;
		 }
	 }
	 
	 /*将inst从prepare instances移到accept instances*/
	 proposer_move_instance(p, p->prepare_instances, p->accept_instances, inst);
	 /*构建一个accept_req消息结构*/
	 return instance_to_accept_req(inst);
}

int proposer_receive_accept_ack(struct proposer* p, accept_ack* ack, prepare_req* out)
{
	khiter_t k = kh_get_instance(p->accept_instances, ack->iid);
	if(k == kh_end(p->accept_instances)){
		paxos_log_debug("Accept ack dropped, iid: %u not pending", ack->iid);
		return 0;
	}

	struct instance* inst = kh_value(p->accept_instances, k);
	if(ack->ballot == inst->ballot){/*提议号相同，认为acceptor同意了提议值*/
		assert(ack->value_ballot == inst->value_ballot);

		if(!quorum_add(&inst->quorum, ack->acceptor_id)){ /*未过半数，继续等待*/
			paxos_log_debug("Duplicate accept dropped from: %d, iid: %u", 
				ack->acceptor_id, inst->iid);
			return 0;
		}

		if(quorum_reached(&inst->quorum)){ /*过半通过后，提议者可以删除数据，在learners会从accpetor上学习到决议结果*/
			paxos_log_debug("Quorum reached for instance %u", inst->iid);
			kh_del_instance(p->accept_instances, k);
			instance_free(inst);
		}

		return 0;
	}
	else{
		paxos_log_debug("Instance %u preempted: ballot %d ack ballot %d", inst->iid, inst->ballot, ack->ballot);
		if(inst->value_ballot == 0)
			carray_push_back(p->values, inst->value); /*值从新回到未提议的队列中，等待下一次提议*/
		else /*可能是非法或者错误的提议，直接丢弃？？*/
			free(inst->value);

		inst->value = NULL;
		/*将提议从新回到第一阶段的开始位置*/
		proposer_move_instance(p, p->accept_instances, p->prepare_instances, inst);
		/*从新尝试第一阶段提议表决,尝试更大的提案号*/
		proposer_preempt(p, inst, out);
		return 1;
	}
}

struct timeout_iterator* proposer_timeout_iterator(struct proposer* p)
{
	struct timeout_iterator* iter = malloc(sizeof(struct timeout_iterator));
	iter->pi = kh_begin(p->prepare_instances);
	iter->ai = kh_begin(p->accept_instances);
	iter->proposer = p;
	gettimeofday(&iter->timeout, NULL);

	return iter;
}
/*获得一个超时的prepare instance*/
static struct instance* next_timedout(khash_t(instance)* h, khiter_t* k, struct timeval* t)
{
	for(; *k != kh_end(h); ++(*k)){
		if(!kh_exist(h, *k))
			continue;

		struct instance* inst = kh_value(h, *k);
		if(quorum_reached(&inst->quorum)) /*大多数通过,超时判断*/
			continue;
		
		if(instance_has_timedout(inst, t)) /*查看超时*/
			return inst;
	}

	return NULL;
}

/*通过一个超时的prepare instance 构建一个prepare_req*/
prepare_req* timeout_iterator_prepare(struct timeout_iterator* iter)
{
	struct instance* inst;
	struct proposer* p = iter->proposer;
	inst = next_timedout(p->prepare_instances, &iter->pi, &iter->timeout); 
	if(inst != NULL){ /*构建一个prepare req*/
		prepare_req* req = malloc(sizeof(prepare_req));
		*req = (prepare_req){inst->iid, inst->ballot};
		inst->created_at = iter->timeout;

		return req;
	}
	return NULL;
}

/*通过一个超时accept instance 构建一个accept req*/
accept_req* timeout_iterator_accept(struct timeout_iterator* iter)
{
	struct instance* inst;
	struct proposer* p = iter->proposer;
	inst = next_timedout(p->accept_instances, &iter->ai, &iter->timeout);
	if (inst != NULL) {
		inst->created_at = iter->timeout;
		return instance_to_accept_req(inst);
	}
	return NULL;
}

void timeout_iterator_free(struct timeout_iterator* iter)
{
	free(iter);
}

static ballot_t	proposer_next_ballot(struct proposer* p, ballot_t b)
{
	/*构建一个更大的提议号*/
	if (b > 0)
		return MAX_N_OF_PROPOSERS + b;
	else
		return MAX_N_OF_PROPOSERS + p->id;
}

static void proposer_preempt(struct proposer* p, struct instance* inst, prepare_req* out)
{
	inst->ballot = proposer_next_ballot(p, inst->ballot);
	inst->value_ballot = 0;

	/*清空接受instance提议的acceptor状态信息*/
	quorum_clear(&inst->quorum);

	/*重启获取一个prepare req*/
	*out = (prepare_req) {inst->iid, inst->ballot};
	gettimeofday(&inst->created_at, NULL);
}

static void proposer_move_instance(struct proposer* p, khash_t(instance)* f, khash_t(instance)* t, struct instance* inst)
{
	int rv;
	khiter_t k;
	k = kh_get_instance(f, inst->iid);
	assert(k != kh_end(f));

	kh_del_instance(f, k);

	k = kh_put_instance(t, inst->iid, &rv);
	assert(rv > 0);
	kh_value(t, k) = inst;
	/*清空接受instance提议的acceptor状态信息，意味着从提议阶段切换*/
	quorum_clear(&inst->quorum);
}

static struct instance* instance_new(iid_t iid, ballot_t ballot, int acceptors)
{
	struct instance* inst;
	inst = malloc(sizeof(struct instance));
	inst->iid = iid;
	inst->ballot = ballot;
	inst->value_ballot = 0;
	inst->value = NULL;

	gettimeofday(&inst->created_at, NULL);

	quorum_init(&inst->quorum, acceptors);
	assert(inst->iid > 0);

	return inst;
}

static void instance_free(struct instance* inst)
{
	quorum_destroy(&inst->quorum);

	if (inst->value != NULL)
		free(inst->value);

	free(inst);
}

static int instance_has_timedout(struct instance* inst, struct timeval* now)
{
	/*判断instance超时*/
	int diff = now->tv_sec - inst->created_at.tv_sec;
	return diff > paxos_config.proposer_timeout;
}

static accept_req* instance_to_accept_req(struct instance* inst)
{
	accept_req* req = malloc(sizeof(accept_req) + inst->value->data_size);
	req->iid = inst->iid;
	req->ballot = inst->ballot;
	req->value_size = inst->value->data_size;
	memcpy(req->value, inst->value->data, req->value_size);

	return req;
}

static paxos_msg* wrap_value(const char* value, size_t size)
{
	/*构建了一个submit消息*/
	paxos_msg* msg = malloc(size + sizeof(paxos_msg));
	msg->data_size = size;
	msg->type = submit;
	memcpy(msg->data, value, size);
	return msg;
}
