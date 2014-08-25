#include "learner.h"
#include "khash.h"
#include <stdlib.h>
#include <assert.h>

/*一个提议的实例描述*/
struct instance
{
	iid_t			iid;					/*提案遍号，全局唯一*/
	ballot_t		last_update_ballot;
	accept_ack**	acks;					/*所有的ack内容*/
	accept_ack*		final_value;			/*大多数通过的ack内容*/
};

/*一个instance的HASH MAP*/
KHASH_MAP_INIT_INT(instance, struct instance*);

struct learner
{
	int				acceptors;				/*确定acceptor的个数*/
	int				late_start;				/*是否需要同步状态信息（iid）*/
	iid_t			current_iid;			/*当前发表的决议ID*/
	iid_t			highest_iid_closed;		/*本地认为大多数通过的提议编号（iid）*/
	khash_t(instance)* instances;
}; 

static struct instance* learner_get_instance(struct learner* l, iid_t iid);
static struct instance* learner_get_current_instance(struct learner* l);
static struct instance* learner_get_instance_or_create(struct learner* l, iid_t iid);

static void				learner_delete_instance(struct learner* l, struct instance* inst);

static struct instance* instance_new(int acceptors);
static void				instance_free(struct instance* i, int acceptors);

static void				instance_update(struct instance* i, accept_ack* ack, int acceptors);
static int				instance_has_quorum(struct instance* i, int acceptors);
static void				instance_add_accept(struct instance* i, accept_ack* ack);

static accept_ack*		accept_ack_dup(accept_ack* ack);

struct learner* learner_new(int acceptors)
{
	struct learner* l = (struct learner*)malloc(sizeof(struct learner));
	l->acceptors = acceptors;
	l->current_iid = 1;
	l->highest_iid_closed = 1;
	l->late_start = !paxos_config.learner_catch_up;
	l->instances = kh_init(instance);

	return l;
}

void learner_free(struct learner* l)
{
	struct instance* inst;
	/*删除所有的实例*/
	kh_foreach_value(l->instances, inst, instance_free(inst, l->acceptors));
	/*释放hash map*/
	kh_destroy(instance, l->instances);
	free(l);
}

/*处理一个来自acceptor的accept ack事件*/
void learner_recevie_accept(struct learner* l, accept_ack* ack)
{
	/*如果是第一个accept ack,且需要处理，进行信息保存作为初始值,相当于上下文衔接*/
	if(l->late_start){
		l->late_start = 0;
		l->current_iid = ack->iid;
	}

	/*已经受理的提案编号，或者太旧的提案*/
	if(ack->iid < l->current_iid){
		paxos_log_debug("Dropped accept_ack for iid %u. Already delivered.", ack->iid);
		return ;
	}

	/*通过ack iid查找或者构建instance,如果没查找到就会构建一个*/
	struct instance* inst = learner_get_instance_or_create(l, ack->iid);

	/*将受到的ack事件的状态加入到instance当中*/
	instance_update(inst, ack, l->acceptors);

	/*已经是大多数，并且实例iid大于已经本地（认为）通过提议中最大的iid,进行highest iid赋值*/
	if(instance_has_quorum(inst, l->acceptors) && (inst->iid > l->highest_iid_closed)){
		l->highest_iid_closed = inst->iid;
	}
}

/*发表一个提案*/
accept_ack* learner_deliver_next(struct learner* l)
{
	struct instance* inst = learner_get_current_instance(l);
	if(inst == NULL)
		return NULL;

	/*提案被大多数通过,进行提案发表*/
	if(instance_has_quorum(inst, l->acceptors)){
		/*构建一个accept ack作为发表通告消息体*/
		accept_ack* ack = accept_ack_dup(inst->final_value);
		/*删除掉发表的提案实例*/
		learner_delete_instance(l, inst);

		l->current_iid ++;/*++,预测下一个发表的提案编号*/
	}
}

int learner_has_holes(struct learner* l, iid_t* f, iid_t* from, iid_t* to)
{
	/*当前的提案编号距l->highest_iid_closed差的位置,中间全是未完成通过的iid*/
	if(l->highest_iid_closed > l->current_iid){
		*from = l->current_iid;
		*to = l->highest_iid_closed;

		return 1;
	}

	return 0;
}

/*通过iid查找提案实例*/
static struct instance* learner_get_instance(struct learner* l, iid_t iid)
{
	khiter_t k = kh_get_instance(l->instances, iid);
	if(k != kh_end(l->instances)){
		return kh_value(l->instances, k);
	}

	return NULL;
}

static struct instance* learner_get_current_instance(struct learner* l)
{
	return learner_get_instance(l, l->current_iid);
}

static struct instance* learner_get_instance_or_create(struct learner* l, iid_t iid)
{
	struct instance* inst = learner_get_instance(l, iid);
	if (inst == NULL) { /*没有找到instance,构建一个新的instance*/
		int rv;
		khiter_t k = kh_put_instance(l->instances, iid, &rv);
		assert(rv != -1);

		inst = instance_new(l->acceptors);
		kh_value(l->instances, k) = inst;
	}

	return inst;
}

static void learner_delete_instance(struct learner* l, struct instance* inst)
{
	khiter_t k;
	k = kh_get_instance(l->instances, inst->iid);
	kh_del_instance(l->instances, k);

	instance_free(inst, l->acceptors);
}

static struct instance* instance_new(int acceptors)
{
	int i;
	struct instance* inst;
	inst = (struct instance*)malloc(sizeof(struct instance));
	memset(inst, 0, sizeof(struct instance));

	/*构建与acceptor数量一致的acks数组，因为一个acceptor只会通过一种提案，所以是一一对应的*/
	inst->acks = (accept_ack**)malloc(sizeof(accept_ack*) * acceptors);
	for (i = 0; i < acceptors; ++i)
		inst->acks[i] = NULL;

	return inst;
}

static void instance_free(struct instance* inst, int acceptors)
{
	int i;
	for (i = 0; i < acceptors; i++){
		if (inst->acks[i] != NULL)
			free(inst->acks[i]);
	}

	free(inst->acks);
	free(inst);
}

static void instance_update(struct instance* inst, accept_ack* ack, int acceptors)
{
	if(inst->iid == 0){ /*未赋值的instance（第一个ack）就进行赋值*/
		paxos_log_debug("Received first message for iid: %u", ack->iid);
		inst->iid = ack->iid;
		inst->last_update_ballot = ack->ballot;
	}

	/*大多数已经通过，可以不处理，如果处理可能会事件重复*/
	if(instance_has_quorum(inst, acceptors)){
		paxos_log_debug("Dropped accept_ack iid %u. Already closed.", ack->iid);
		return;
	}

	/*判断ack是否发重了*/
	accept_ack* prev_ack = inst->acks[ack->acceptor_id];
	if(prev_ack != NULL && prev_ack->ballot >= ack->ballot){
		paxos_log_debug("Dropped accept_ack for iid %u. Previous ballot is newer or equal.", ack->iid);
		return;
	}
	
	instance_add_accept(inst, ack);
}

static int instance_has_quorum(struct instance* inst, int acceptors)
{
	accept_ack* curr_ack;
	int i, a_valid_index = -1, count = 0;

	/*已经完成大多数接受，并且将大多数的ack内容记录在final_value*/
	if(inst->final_value != NULL)
		return 1;

	for(i = 0; i < acceptors; i++){
		curr_ack = inst->acks[i];
		if(curr_ack == NULL)
			continue;

		if (curr_ack->ballot == inst->last_update_ballot) { /*同意的决议ID*/
			count ++;
			a_valid_index = i;

			/*在acceptor上记录已经大多数通过，直接标识为大多数通过状态*/
			if (curr_ack->is_final){
				count += acceptors;
				break;
			}
		}
	}

	/*判断是否是大多数通过，如果是标识通过的值*/
	if(count >= paxos_quorum(acceptors)){
		paxos_log_debug("Reached quorum, iid: %u is closed!", inst->iid);
		inst->final_value = inst->acks[a_valid_index];
		return 1;
	}

	return 0;
}

static void instance_add_accept(struct instance* inst, accept_ack* ack)
{
	if (inst->acks[ack->acceptor_id] != NULL){ /*已经保存了ack*/
		free(inst->acks[ack->acceptor_id]); /*释放掉旧的*/
	}

	/*替换成了新的*/
	inst->acks[ack->acceptor_id] = accept_ack_dup(ack);
	inst->last_update_ballot = ack->ballot;
}

/*分配一个accept ack对象并对ack进行复制*/
static accept_ack* accept_ack_dup(accept_ack* ack)
{
	accept_ack* copy = (accept_ack *)malloc(ACCEPT_ACK_SIZE(ack));
	memcpy(copy, ack, ACCEPT_ACK_SIZE(ack));

	return copy;
}
