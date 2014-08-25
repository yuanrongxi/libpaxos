#include "carray.h"
#include <stdlib.h>
#include <assert.h>

struct carray
{
	int		head;		/*head index of array*/
	int		tail;		/*tail index of array*/
	int		size;		/*array's size*/
	int		count;		/*count of values*/
	void**	array;		/*array ptr*/
};

static int carray_full(struct carray* a);

struct carray* carray_new(int size)
{
	struct carray* a = (struct carray *)malloc(sizeof(struct carray));
	assert(a && size > 0);

	a->head = 0;
	a->tail = 0;
	a->size = size;
	a->count = 0;
	a->array = (void**)malloc(sizeof(void *) * size);
	assert(a->array);

	return a;
}

static void carray_grow(struct carray* a)
{
	int i, count;
	struct carray* tmp = carray_new(a->size * 2);
	count = carray_count(a);
	for(i = 0; i < count;  i ++){
		carray_push_back(tmp, carray_at(a, i));
	}
	a->head = 0;
	a->tail = tmp->tail;
	a->size = tmp->size;
	a->array = tmp->array;

	free(tmp);
}

void carray_free(struct carray* a)
{
	if(a != NULL){
		free(a->array);
		free(a);
	}
}

static int carray_full(struct carray* a)
{
	return a->count == a->size;
}

int carray_empty(struct carray* a)
{
	return a->count == 0;
}

int carray_size(struct carray* a)
{
	return a->size;
}

int carray_push_back(struct carray* a, void* p)
{
	if(carray_full(a))
		carray_grow(a);

	a->array[a->tail] = p;
	a->tail = (a->tail + 1) % a->size;
	a->count ++;
	
	return 0;
}

int carray_push_front(struct carray* a, void* p)
{
	if(carray_full(a))
		carray_grow(a);

	if(carray_empty(a))
		return carray_push_back(a, p);
	 
	if (a->head - 1 >= 0)
		a->head--;
	else
		a->head = a->size - 1;
	a->array[a->head] = p;
	a->count++;

	return 0;
}

void* carray_front(struct carray* a) 
{
	if (carray_empty(a)) 
		return NULL;

	return a->array[a->head];
}

void* carray_pop_front(struct carray* a) 
{
	void* p;
	if (carray_empty(a)) 
		return NULL;

	p = a->array[a->head];
	a->head = (a->head + 1) % a->size;
	a->count--;

	return p;
}

int carray_count(struct carray* a)
{
	return a->count;
}


void* carray_at(struct carray* a, int i)
{
	if(carray_empty(a))
		return NULL;

	return a->array[(a->head + i) % a->size];
}

int carray_count_match(struct carray* a, match_fn_t match_fn, void* arg) 
{
	int i, count = 0;
	for (i = 0; i < carray_count(a); i++)
		if (match_fn(arg, carray_at(a, i)))
			count++;
	return count;
}

static struct carray* _carray_map(struct carray* a, match_fn_t match_fn, void* arg, int match) 
{
	int i;
	void* p;
	struct carray* nw = carray_new(a->size);
	for (i = 0; i < carray_count(a); i++) {
		p = carray_at(a, i);
		if (match_fn(arg, p) == match)
			carray_push_back(nw, p);
	}
	return nw;
}

struct carray* carray_collect(struct carray* a, match_fn_t match_fn, void* arg) 
{
	return _carray_map(a, match_fn, arg, 1);
}

struct carray* carray_reject(struct carray* a, match_fn_t match_fn, void* arg) 
{
	return _carray_map(a, match_fn, arg, 0);
}