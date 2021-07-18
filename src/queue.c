/* $Id: queue.c 3347 2005-12-27 20:44:42Z tron $ */

#include "stdafx.h"
#include "openttd.h"
#include "queue.h"
#include "vehicle.h"

static void Stack_Clear(Queue* q, bool free_values)
{
	uint i;
	if (free_values)
		for (i=0;i<q->data.stack.size;i++)
			free(q->data.stack.elements[i]);
	q->data.stack.size = 0;
}

static void Stack_Free(Queue* q, bool free_values)
{
	q->clear(q, free_values);
	free(q->data.stack.elements);
	if (q->freeq)
		free(q);
}

static bool Stack_Push(Queue* q, void* item, int priority)
{
	if (q->data.stack.size == q->data.stack.max_size)
		return false;
	q->data.stack.elements[q->data.stack.size++] = item;
	return true;
}

static void* Stack_Pop(Queue* q)
{
	void* result;
	if (q->data.stack.size == 0)
		return NULL;
	result = q->data.stack.elements[--q->data.stack.size];

	return result;
}

static bool Stack_Delete(Queue* q, void* item, int priority)
{
	return false;
}

static Queue* init_stack(Queue* q, uint max_size)
{
	q->push = Stack_Push;
	q->pop = Stack_Pop;
	q->del = Stack_Delete;
	q->clear = Stack_Clear;
	q->free = Stack_Free;
	q->data.stack.max_size = max_size;
	q->data.stack.size = 0;
	q->data.stack.elements = malloc(max_size * sizeof(void*));
	q->freeq = false;
	return q;
}

Queue* new_Stack(uint max_size)
{
	Queue* q = malloc(sizeof(Queue));
	init_stack(q, max_size);
	q->freeq = true;
	return q;
}

/*
 * Fifo
 */

static void Fifo_Clear(Queue* q, bool free_values)
{
	uint head, tail;
	if (free_values) {
		head = q->data.fifo.head;
		tail = q->data.fifo.tail; /* cache for speed */
		while (head != tail) {
			free(q->data.fifo.elements[tail]);
			tail = (tail + 1) % q->data.fifo.max_size;
		}
	}
	q->data.fifo.head = q->data.fifo.tail = 0;
}

static void Fifo_Free(Queue* q, bool free_values)
{
	q->clear(q, free_values);
	free(q->data.fifo.elements);
	if (q->freeq)
		free(q);
}

static bool Fifo_Push(Queue* q, void* item, int priority)
{
	uint next = (q->data.fifo.head + 1) % q->data.fifo.max_size;
	if (next == q->data.fifo.tail)
		return false;
	q->data.fifo.elements[q->data.fifo.head] = item;


	q->data.fifo.head = next;
	return true;
}

static void* Fifo_Pop(Queue* q)
{
	void* result;
	if (q->data.fifo.head == q->data.fifo.tail)
		return NULL;
	result = q->data.fifo.elements[q->data.fifo.tail];


	q->data.fifo.tail = (q->data.fifo.tail + 1) % q->data.fifo.max_size;
	return result;
}

static void* Fifo_GetTop(Queue* q)
{
	void* result;
	if (q->data.fifo.head == q->data.fifo.tail)
		return NULL;
	result = q->data.fifo.elements[q->data.fifo.tail];

	// Only getting top, do not take off
	//q->data.fifo.tail = (q->data.fifo.tail + 1) % q->data.fifo.max_size;
	return result;
}

static bool Fifo_Delete(Queue* q, void* item, int priority)
{
	return false;
}

static Queue* init_fifo(Queue* q, uint max_size)
{
	q->push = Fifo_Push;
	q->pop = Fifo_Pop;
	q->del = Fifo_Delete;
	q->clear = Fifo_Clear;
	q->free = Fifo_Free;
	q->data.fifo.max_size = max_size;
	q->data.fifo.head = 0;
	q->data.fifo.tail = 0;
	q->data.fifo.elements = malloc(max_size * sizeof(void*));
	q->freeq = false;
	q->getTop = Fifo_GetTop;
	return q;
}

Queue* new_Fifo(uint max_size)
{
	Queue* q = malloc(sizeof(Queue));
	init_fifo(q, max_size);
	q->freeq = true;
	return q;
}


/*
 * Insertion Sorter
 */

static void InsSort_Clear(Queue* q, bool free_values)
{
	InsSortNode* node = q->data.inssort.first;
	InsSortNode* prev;
	while (node != NULL) {
		if (free_values)
			free(node->item);
		prev = node;
		node = node->next;
		free(prev);

	}
	q->data.inssort.first = NULL;
}

static void InsSort_Free(Queue* q, bool free_values)
{
	q->clear(q, free_values);
	if (q->freeq)
		free(q);
}

static bool InsSort_Push(Queue* q, void* item, int priority)
{
	InsSortNode* newnode = malloc(sizeof(InsSortNode));
	if (newnode == NULL) return false;
	newnode->item = item;
	newnode->priority = priority;
	if (q->data.inssort.first == NULL || q->data.inssort.first->priority >= priority) {
		newnode->next = q->data.inssort.first;
		q->data.inssort.first = newnode;
	} else {
		InsSortNode* node = q->data.inssort.first;
		while( node != NULL ) {
			if (node->next == NULL || node->next->priority >= priority) {
				newnode->next = node->next;
				node->next = newnode;
				break;
			}
			node = node->next;
		}
	}
	return true;
}

static void* InsSort_Pop(Queue* q)
{
	InsSortNode* node = q->data.inssort.first;
	void* result;
	if (node == NULL)
		return NULL;
	result = node->item;
	q->data.inssort.first = q->data.inssort.first->next;
	if (q->data.inssort.first)
		assert(q->data.inssort.first->priority >= node->priority);
	free(node);
	return result;
}

static bool InsSort_Delete(Queue* q, void* item, int priority)
{
	return false;
}

void init_InsSort(Queue* q) {
	q->push = InsSort_Push;
	q->pop = InsSort_Pop;
	q->del = InsSort_Delete;
	q->clear = InsSort_Clear;
	q->free = InsSort_Free;
	q->data.inssort.first = NULL;
	q->freeq = false;
}

Queue* new_InsSort(void)
{
	Queue* q = malloc(sizeof(Queue));
	init_InsSort(q);
	q->freeq = true;
	return q;
}


/*
 * Binary Heap
 * For information, see: http://www.policyalmanac.org/games/binaryHeaps.htm
 */

#define BINARY_HEAP_BLOCKSIZE (1 << BINARY_HEAP_BLOCKSIZE_BITS)
#define BINARY_HEAP_BLOCKSIZE_MASK (BINARY_HEAP_BLOCKSIZE-1)

// To make our life easy, we make the next define
//  Because Binary Heaps works with array from 1 to n,
//  and C with array from 0 to n-1, and we don't like typing
//  q->data.binaryheap.elements[i-1] every time, we use this define.
#define BIN_HEAP_ARR(i) q->data.binaryheap.elements[((i)-1) >> BINARY_HEAP_BLOCKSIZE_BITS][((i)-1) & BINARY_HEAP_BLOCKSIZE_MASK]

static void BinaryHeap_Clear(Queue* q, bool free_values)
{
	/* Free all items if needed and free all but the first blocks of
	 * memory */
	uint i,j;
	for (i=0;i<q->data.binaryheap.blocks;i++) {
		if (q->data.binaryheap.elements[i] == NULL) {
			/* No more allocated blocks */
			break;
		}
		/* For every allocated block */
		if (free_values)
			for (j=0;j<(1<<BINARY_HEAP_BLOCKSIZE_BITS);j++) {
				/* For every element in the block */
				if ((q->data.binaryheap.size >> BINARY_HEAP_BLOCKSIZE_BITS) == i
					&& (q->data.binaryheap.size & BINARY_HEAP_BLOCKSIZE_MASK) == j)
					break; /* We're past the last element */
				free(q->data.binaryheap.elements[i][j].item);
			}
		if (i != 0) {
			/* Leave the first block of memory alone */
			free(q->data.binaryheap.elements[i]);
			q->data.binaryheap.elements[i] = NULL;
		}
	}
	q->data.binaryheap.size = 0;
	q->data.binaryheap.blocks = 1;
}

static void BinaryHeap_Free(Queue* q, bool free_values)
{
	uint i;
	q->clear(q, free_values);
	for (i=0;i<q->data.binaryheap.blocks;i++) {
		if (q->data.binaryheap.elements[i] == NULL)
			break;
		free(q->data.binaryheap.elements[i]);
	}
	free(q->data.binaryheap.elements);
	if (q->freeq)
		free(q);
}

static bool BinaryHeap_Push(Queue* q, void* item, int priority)
{
	#ifdef QUEUE_DEBUG
			printf("[BinaryHeap] Pushing an element. There are %d elements left\n", q->data.binaryheap.size);
	#endif
	if (q->data.binaryheap.size == q->data.binaryheap.max_size)
		return false;
	assert(q->data.binaryheap.size < q->data.binaryheap.max_size);

	if (q->data.binaryheap.elements[q->data.binaryheap.size >> BINARY_HEAP_BLOCKSIZE_BITS] == NULL) {
		/* The currently allocated blocks are full, allocate a new one */
		assert((q->data.binaryheap.size & BINARY_HEAP_BLOCKSIZE_MASK) == 0);
		q->data.binaryheap.elements[q->data.binaryheap.size >> BINARY_HEAP_BLOCKSIZE_BITS] = malloc(BINARY_HEAP_BLOCKSIZE * sizeof(BinaryHeapNode));
		q->data.binaryheap.blocks++;
#ifdef QUEUE_DEBUG
		printf("[BinaryHeap] Increasing size of elements to %d nodes\n",q->data.binaryheap.blocks *  BINARY_HEAP_BLOCKSIZE);
#endif
	}

	// Add the item at the end of the array
	BIN_HEAP_ARR(q->data.binaryheap.size+1).priority = priority;
	BIN_HEAP_ARR(q->data.binaryheap.size+1).item = item;
	q->data.binaryheap.size++;

	// Now we are going to check where it belongs. As long as the parent is
	// bigger, we switch with the parent
	{
		int i, j;
		BinaryHeapNode temp;
		i = q->data.binaryheap.size;
		while (i > 1) {
			// Get the parent of this object (divide by 2)
			j = i / 2;
			// Is the parent bigger then the current, switch them
			if (BIN_HEAP_ARR(i).priority <= BIN_HEAP_ARR(j).priority) {
				temp = BIN_HEAP_ARR(j);
				BIN_HEAP_ARR(j) = BIN_HEAP_ARR(i);
				BIN_HEAP_ARR(i) = temp;
				i = j;
			} else {
				// It is not, we're done!
				break;
			}
		}
	}

	return true;
}

static bool BinaryHeap_Delete(Queue* q, void* item, int priority)
{
	#ifdef QUEUE_DEBUG
			printf("[BinaryHeap] Deleting an element. There are %d elements left\n", q->data.binaryheap.size);
	#endif
	uint i = 0;
	// First, we try to find the item..
	do {
		if (BIN_HEAP_ARR(i+1).item == item) break;
		i++;
	} while (i < q->data.binaryheap.size);
	// We did not find the item, so we return false
	if (i == q->data.binaryheap.size) return false;

	// Now we put the last item over the current item while decreasing the size of the elements
	q->data.binaryheap.size--;
	BIN_HEAP_ARR(i+1) = BIN_HEAP_ARR(q->data.binaryheap.size+1);

	// Now the only thing we have to do, is resort it..
	// On place i there is the item to be sorted.. let's start there
	{
		uint j;
		BinaryHeapNode temp;
		// Because of the fast that Binary Heap uses array from 1 to n, we need to increase
		//   i with 1
		i++;

		for (;;) {
			j = i;
			// Check if we have 2 childs
			if (2*j+1 <= q->data.binaryheap.size) {
				// Is this child smaller than the parent?
				if (BIN_HEAP_ARR(j).priority >= BIN_HEAP_ARR(2*j).priority) {i = 2*j; }
				// Yes, we _need_ to use i here, not j, because we want to have the smallest child
				//  This way we get that straight away!
				if (BIN_HEAP_ARR(i).priority >= BIN_HEAP_ARR(2*j+1).priority) { i = 2*j+1; }
			// Do we have one child?
			} else if (2*j <= q->data.binaryheap.size) {
				if (BIN_HEAP_ARR(j).priority >= BIN_HEAP_ARR(2*j).priority) { i = 2*j; }
			}

			// One of our childs is smaller than we are, switch
			if (i != j) {
				temp = BIN_HEAP_ARR(j);
				BIN_HEAP_ARR(j) = BIN_HEAP_ARR(i);
				BIN_HEAP_ARR(i) = temp;
			} else {
				// None of our childs is smaller, so we stay here.. stop :)
				break;
			}
		}
	}

	return true;
}

static void* BinaryHeap_Pop(Queue* q)
{
	#ifdef QUEUE_DEBUG
			printf("[BinaryHeap] Popping an element. There are %d elements left\n", q->data.binaryheap.size);
	#endif
	void* result;
	if (q->data.binaryheap.size == 0)
		return NULL;

	// The best item is always on top, so give that as result
	result = BIN_HEAP_ARR(1).item;
	// And now we should get ride of this item...
	BinaryHeap_Delete(q,BIN_HEAP_ARR(1).item, BIN_HEAP_ARR(1).priority);

	return result;
}

void init_BinaryHeap(Queue* q, uint max_size)
{
	assert(q);
	q->push = BinaryHeap_Push;
	q->pop = BinaryHeap_Pop;
	q->del = BinaryHeap_Delete;
	q->clear = BinaryHeap_Clear;
	q->free = BinaryHeap_Free;
	q->data.binaryheap.max_size = max_size;
	q->data.binaryheap.size = 0;
	// We malloc memory in block of BINARY_HEAP_BLOCKSIZE
	//   It autosizes when it runs out of memory
	q->data.binaryheap.elements = calloc(1, ((max_size - 1) / BINARY_HEAP_BLOCKSIZE*sizeof(BinaryHeapNode)) + 1);
	q->data.binaryheap.elements[0] = malloc(BINARY_HEAP_BLOCKSIZE * sizeof(BinaryHeapNode));
	q->data.binaryheap.blocks = 1;
	q->freeq = false;
#ifdef QUEUE_DEBUG
		printf("[BinaryHeap] Initial size of elements is %d nodes\n",(1024));
#endif
}

Queue* new_BinaryHeap(uint max_size) {
	Queue* q = malloc(sizeof(Queue));
	init_BinaryHeap(q, max_size);
	q->freeq = true;
	return q;
}

// Because we don't want anyone else to bother with our defines
#undef BIN_HEAP_ARR

/*
 * Hash
 */

void init_Hash(Hash* h, Hash_HashProc* hash, uint num_buckets) {
	/* Allocate space for the Hash, the buckets and the bucket flags */
	uint i;
	assert(h);
	#ifdef HASH_DEBUG
	debug("Allocated hash: %p", h);
	#endif
	h->hash = hash;
	h->size = 0;
	h->num_buckets = num_buckets;
	h->buckets = malloc(num_buckets * (sizeof(HashNode) + sizeof(bool)));
	#ifdef HASH_DEBUG
	debug("Buckets = %p", h->buckets);
	#endif
	h->buckets_in_use = (bool*)(h->buckets + num_buckets);
	h->freeh = false;
	for (i=0;i<num_buckets;i++)
		h->buckets_in_use[i] = false;
}

Hash* new_Hash(Hash_HashProc* hash, int num_buckets) {
	Hash* h = malloc(sizeof(Hash));
	init_Hash(h, hash, num_buckets);
	h->freeh = true;
	return h;
}

void delete_Hash(Hash* h, bool free_values) {
	uint i;
	/* Iterate all buckets */
	for (i=0;i<h->num_buckets;i++)
	{
		if (h->buckets_in_use[i]) {
			HashNode* node;
			/* Free the first value */
			if (free_values)
				free(h->buckets[i].value);
			node = h->buckets[i].next;
			while (node != NULL) {
				HashNode* prev = node;
				node = node->next;
				/* Free the value */
				if (free_values)
					free(prev->value);
				/* Free the node */
				free(prev);
			}
		}
	}
	free(h->buckets);
	/* No need to free buckets_in_use, it is always allocated in one
	 * malloc with buckets */
	#ifdef HASH_DEBUG
	debug("Freeing Hash: %p", h);
	#endif
	if (h->freeh)
		free(h);
}

#ifdef HASH_STATS
static void stat_Hash(Hash* h)
{
	uint used_buckets = 0;
	uint max_collision = 0;
	uint max_usage = 0;
	uint usage[200];
	uint i;
	uint collision;
	HashNode* node;

	for (i=0;i<200;i++) usage[i] = 0;
	for (i=0;i<h->num_buckets;i++) {
		collision = 0;
		if (h->buckets_in_use[i]) {
			used_buckets++;
			node = &h->buckets[i];
			while (node != NULL) {
				collision++;
				node = node->next;
			}
			if (collision > max_collision) max_collision = collision;
		}
		if (collision > 199) collision = 199;
		usage[collision]++;
		if (collision >0 && usage[collision] >= max_usage) max_usage = usage[collision];
	}
	printf("---\nHash size: %d\nNodes used: %d\nNon empty buckets: %d\nMax collision: %d\n", h->num_buckets, h->size, used_buckets, max_collision);
	printf("{ ");
	for (i=0;i<=max_collision;i++)
		if (usage[i]) {
			printf("%d:%d ", i, usage[i]);
/*
			if (i>0){
				uint j;
				for (j=0;j<(usage[i] * 160 / 800);j++)
					printf("#");
			}
			printf("\n");
			*/
		}
	printf ("}\n");
}
#endif

void clear_Hash(Hash* h, bool free_values)
{
	uint i;
	HashNode* node;
#ifdef HASH_STATS
	if (h->size > 2000)
		stat_Hash(h);
#endif
	/* Iterate all buckets */
	for (i=0;i<h->num_buckets;i++)
	{
		if (h->buckets_in_use[i]) {
			h->buckets_in_use[i] = false;
			/* Free the first value */
			if (free_values)
				free(h->buckets[i].value);
			node = h->buckets[i].next;
			while (node != NULL) {
				HashNode* prev = node;
				node = node->next;
				if (free_values)
					free(prev->value);
				free(prev);
			}
		}
	}
	h->size = 0;
}

/* Finds the node that that saves this key pair. If it is not
 * found, returns NULL. If it is found, *prev is set to the
 * node before the one found, or if the node found was the first in the bucket
 * to NULL. If it is not found, *prev is set to the last HashNode in the
 * bucket, or NULL if it is empty. prev can also be NULL, in which case it is
 * not used for output.
 */
static HashNode* Hash_FindNode(Hash* h, uint key1, uint key2, HashNode** prev_out)
{
	uint hash = h->hash(key1, key2);
	HashNode* result = NULL;
	#ifdef HASH_DEBUG
	debug("Looking for %u, %u", key1, key2);
	#endif
	/* Check if the bucket is empty */
	if (!h->buckets_in_use[hash]) {
		if (prev_out)
			*prev_out = NULL;
		result = NULL;
	/* Check the first node specially */
	} else if (h->buckets[hash].key1 == key1 && h->buckets[hash].key2 == key2) {
		/* Save the value */
		result = h->buckets + hash;
		if (prev_out)
			*prev_out = NULL;
	#ifdef HASH_DEBUG
		debug("Found in first node: %p", result);
	#endif
	/* Check all other nodes */
	} else {
		HashNode* prev = h->buckets + hash;
		HashNode* node = prev->next;
		while (node != NULL) {
			if (node->key1 == key1 && node->key2 == key2) {
				/* Found it */
				result = node;
	#ifdef HASH_DEBUG
				debug("Found in other node: %p", result);
	#endif
				break;
			}
			prev = node;
			node = node->next;
		}
		if (prev_out)
			*prev_out = prev;
	}
	#ifdef HASH_DEBUG
	if (result == NULL)
		debug("Not found");
	#endif
	return result;
}

void* Hash_Delete(Hash* h, uint key1, uint key2) {
	void* result;
	HashNode* prev; /* Used as output var for below function call */
	HashNode* node = Hash_FindNode(h, key1, key2, &prev);

	if (node == NULL) {
		/* not found */
		result = NULL;
	} else if (prev == NULL) {
		/* It is in the first node, we can't free that one, so we free
		 * the next one instead (if there is any)*/
		/* Save the value */
		result = node->value;
		if (node->next != NULL) {
			HashNode* next = node->next;
			/* Copy the second to the first */
			*node = *next;
			/* Free the second */
		#ifndef NOFREE
			free(next);
		#endif
		} else {
			/* This was the last in this bucket */
			/* Mark it as empty */
			uint hash = h->hash(key1, key2);
			h->buckets_in_use[hash] = false;
		}
	} else {
		/* It is in another node */
		/* Save the value */
		result = node->value;
		/* Link previous and next nodes */
		prev->next = node->next;
		/* Free the node */
		#ifndef NOFREE
		free(node);
		#endif
	}
	if (result != NULL)
		h->size--;
	return result;
}


void* Hash_Set(Hash* h, uint key1, uint key2, void* value) {
	HashNode* prev;
	HashNode* node = Hash_FindNode(h, key1, key2, &prev);
	void* result = NULL;
	if (node != NULL) {
		/* Found it */
		result = node->value;
		node->value = value;
		return result;
	}
	/* It is not yet present, let's add it */
	if (prev == NULL) {
		/* The bucket is still empty */
		uint hash = h->hash(key1, key2);
		h->buckets_in_use[hash] = true;
		node = h->buckets + hash;
	} else {
		/* Add it after prev */
		node = malloc(sizeof(HashNode));
		prev->next = node;
	}
	node->next = NULL;
	node->key1 = key1;
	node->key2 = key2;
	node->value = value;
	h->size++;
	return NULL;
}

void* Hash_Get(Hash* h, uint key1, uint key2) {
	HashNode* node = Hash_FindNode(h, key1, key2, NULL);
	#ifdef HASH_DEBUG
	debug("Found node: %p", node);
	#endif
	if (node == NULL) {
		/* Node not found */
		return NULL;
	} else {
		return node->value;
	}
}

uint Hash_Size(Hash* h) {
    return h->size;
}
/*
// Returns success
bool VPush(VehicleQueue *q, Vehicle *v)
{
	VQueueItem* newItem;

	// Do not push NULLs
	assert(v != NULL);

	if(q->size == 0x7FFFFFFF)
		return false;

	newItem = malloc(sizeof(VQueueItem));

	if(newItem == NULL)
		return false;

	if(q->size == 0) {
		assert(q->top == NULL);
		assert(q->bottom == NULL);
		newItem->data = v;
		newItem->position = 1;
		newItem->queue = q;
		newItem->above = NULL;
		newItem->below = NULL;
		q->bottom = newItem;
		q->top = newItem;
		v->queue_item = newItem;
		q->size = 1;
		return true;
	}

	q->bottom->below = newItem;
	newItem->above = q->bottom;
	newItem->below = NULL;
	newItem->data = v;
	newItem->position = q->bottom->position + 1;
	newItem->queue = q;
	v->queue_item = newItem;
	q->bottom = newItem;
	q->size++;
	return true;
}

// Returns vehicle popped
Vehicle* VPop(VehicleQueue *q)
{
	Vehicle* v;
	VQueueItem* oldItem;
	if(q->size == 0)
		return NULL;

	oldItem = q->top;

	assert(oldItem != NULL);

	q->top = oldItem->below;
	if(q->top != NULL)
		q->top->above = NULL;

	// Had one item, now empty
	if(q->size == 1) {
		q->bottom = NULL;
		q->top = NULL;
		q->size = 0;
		q->offset = 0;
		q->dirty = false;
		v = oldItem->data;
		v->queue_item = NULL;
		free(oldItem);
		return v;
	}

	// Top was above bottom - now top *IS* bottom
	if(q->size == 2)
		q->bottom->above = NULL;

	v = oldItem->data;

	v->queue_item = NULL;

	free(oldItem);

	q->offset++;
	q->size--;

	if(q->offset > 0x7FFFFFFF) {
		q->dirty = true;
		q->clean(q);
	}
	return v;
}

Vehicle* VGetTop(VehicleQueue *q)
{
	if(q->size != 0)
		return q->top->data;
	else
		return NULL;
}

void VClean(VehicleQueue *q)
{
	bool done;
	uint32 currentSize;
	VQueueItem* currItem;
	done = false;

	// Empty queue
	if(q->top == NULL) {
		assert(q->bottom == NULL);
		assert(q->size == 0);
		q->offset = 0;
		q->dirty = false;
		return;
	}

	currItem = q->top;
	q->offset = 0;

	currentSize = 1;
	while(done == false) {
		currItem->position = currentSize;
		currItem = currItem->below;
		if(currItem == NULL) {
			done = true;
			assert(q->size == currentSize);
		}
		currentSize++;
	}

	// Congrats! We now have a clean queue!
	q->dirty = false;
	return;
}

void VClear(VehicleQueue *q)
{
	while(q->pop(q) != NULL) {
		// What? Expecting something? The clearing is done
		// in the while statement above - I don't need anything here!
	}
	q->clean(q);
	return;
}

// This is one of the special functions - allows item to take itself off
// the queue no matter where in the queue it is!
void VDelete(VehicleQueue *q, Vehicle *v)
{
	VQueueItem* current;
	VQueueItem* above;
	VQueueItem* below;

	current = v->queue_item;
	if(current == NULL)
		return;

	if(current == q->top)
		q->top = current->below;

	if(current == q->bottom)
		q->bottom = current->above;

	above = current->above;
	below = current->below;

	if(above != NULL)
		above->below = below;
	
	if(below != NULL)
		below->above = above;

	v->queue_item = NULL;

	free(current);

	q->size--;
	q->dirty = true;
}

// Returns 0 if item does not exist, returns position otherwise.
uint32 VGetPos(VehicleQueue *q, Vehicle *v)
{
	if(q->dirty)
		q->clean(q);

	if(v->queue_item == NULL)
		return 0;

	return v->queue_item->position - q->offset;
}

VehicleQueue* new_VQueue()
{
	VehicleQueue* q = malloc(sizeof(VehicleQueue));

	q->push = VPush;
	q->pop = VPop;
	q->getTop = VGetTop;
	q->clean = VClean;
	q->clear = VClear;
	q->del = VDelete;
	q->getPos = VGetPos;

	q->size = 0;
	q->offset = 0;
	q->top = NULL;
	q->bottom = NULL;
	q->dirty = false;
	
	return q;
}
