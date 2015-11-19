/* 
 * myth_sleep_queue_func.h
 */
#pragma once
#ifndef MYTH_SLEEP_QUEUE_FUNC_H_
#define MYTH_SLEEP_QUEUE_FUNC_H_

#include <assert.h>
#include "myth/myth_sleep_queue.h"

#if MYTH_SLEEP_QUEUE_LOCK
#include "myth_internal_lock_func.h"
#endif

#if !MYTH_SLEEP_QUEUE_LOCK
/* stuff needed for non-blocking version */

static inline myth_sleep_queue_item_t load_(volatile myth_sleep_queue_item_t * p) {
  myth_sleep_queue_item_t q = *p;
  return q;
}

static inline void store_(volatile myth_sleep_queue_item_t * p, 
			  myth_sleep_queue_item_t t) {
  *p = t;
}

static inline int cas_(volatile myth_sleep_queue_item_t * p, 
		       myth_sleep_queue_item_t old, 
		       myth_sleep_queue_item_t val) {
  return __sync_bool_compare_and_swap(p, old, val);
} 

#if MYTH_QUEUE_DBG
static inline myth_queue_dbg_print_first(int x, int _) {
  (void)_;
  printf("%d\n", x);
  return x;
}
#define load(addr) load_(addr, printf("%d : load(%s)\n", me, #addr))
#define store(addr, val) store_(addr, val, printf("%d : store(%s, %s)\n", #addr, #val))
#define cas(addr, old, val) myth_queue_dbg_print_first(cas_(addr, old, val), printf("%d : cas(%s,%s,%s) -> ", #addr, #old, #val))

#else  /* MYTH_QUEUE_DBG */
#define load(addr) load_(addr)
#define store(addr, val) store_(addr, val)
#define cas(addr, old, val) cas_(addr, old, val)
#endif	/* MYTH_QUEUE_DBG */

/* 1 if p's last bit is set */
static inline size_t last_bit(myth_sleep_queue_item_t p) {
  size_t q = (size_t)p;
  return (q & 1);
}

/* assuming p's last bit is zero, return p but whose
   last bit is set */
static inline myth_sleep_queue_item_t set_last_bit(myth_sleep_queue_item_t p) {
  assert(last_bit(p) == 0);
  void * q = p;
  return q + 1;
}

/* assuming p's last bit is set, return p but whose
   last bit is cleared */
static inline myth_sleep_queue_item_t clear_last_bit(myth_sleep_queue_item_t p) {
  assert(last_bit(p));
  void * q = p;
  return q - 1;
}
#endif	/* ! MYTH_SLEEP_QUEUE_LOCK */

static inline void myth_sleep_queue_init(myth_sleep_queue_t * q) {
  q->head = q->tail = 0;
#if MYTH_SLEEP_QUEUE_LOCK
  myth_internal_lock_init(q->ilock);
#endif
}

static inline void myth_sleep_queue_destroy(myth_sleep_queue_t * q) {
  assert(q->head == 0);
  assert(q->tail == 0);
#if MYTH_SLEEP_QUEUE_LOCK
  myth_internal_lock_destroy(q->ilock);
#endif
}

#if MYTH_SLEEP_QUEUE_LOCK

static inline long myth_sleep_queue_enq(myth_sleep_queue_t * q, 
					myth_sleep_queue_item_t t) {
  t->next = 0;
  myth_internal_lock_lock(q->ilock);
  myth_sleep_queue_item_t tail = q->tail;
  if (tail) {
    tail->next = t;
  } else {
    q->head = t;
  }
  q->tail = t;
  myth_internal_lock_unlock(q->ilock);
  return 0;		/* done */
}

static inline myth_sleep_queue_item_t myth_sleep_queue_deq(myth_sleep_queue_t * q) {
  myth_internal_lock_lock(q->ilock);
  myth_sleep_queue_item_t head = q->head;
  if (head) {
    myth_sleep_queue_item_t next = head->next;
    q->head = next;
    if (!next) {
      q->tail = 0;
    }
  }
  myth_internal_lock_unlock(q->ilock);
  return head;		/* done */
}

#else  /* MYTH_SLEEP_QUEUE_LOCK */
/* enqueue an element t to q.  */
static inline long myth_sleep_queue_enq(myth_sleep_queue_t * q, 
					myth_sleep_queue_item_t t) {
  t->next = 0;
  while (1) {
    /* look at tail */
    myth_sleep_queue_item_t tail = load(&q->tail);
    if (!tail) {
      /* empty. try to let tail point to t */
      if (cas(&q->tail, 0, t)) {
	/* empty -> singleton.
	   let head also point to t. 
	   head MUST ALSO BE NULL, but to catch
	   a bug, we check it is in fact a NULL */
	myth_sleep_queue_item_t head = load(&q->head);
	assert(!head);
	store(&q->head, t);
	return 0;		/* done */
      } 
    } else {
      /* not empty */
      myth_sleep_queue_item_t next = load(&tail->next);
      if (last_bit(next)) {
	/* the last bit of the next field is
	   set, which means this item just
	   has been logically removed by deq.
	   this happens when enq and deq
	   happen almost at the same time on
	   a singleton queue.

	   the dequeuer sooner or later sets both
	   head and tail to NULL, but this thread
	   also helps the dequeuer make the progress
	*/
	cas(&q->head, tail, clear_last_bit(next));
	cas(&q->tail, tail, clear_last_bit(next)); 
	continue;
      }
      /* tail is still in the queue.  try to
	 let tail point to t if its next
	 field is NULL. use CAS as other
	 threads may be doing the same */
      if (cas(&tail->next, 0, t)) {
	/* I won. tail->next now points to t.
	   advance q->tail */
	cas(&q->tail, tail, t);
	return 0;
      }
      /* I lost. somebody else has just
	 appended a node to tail. advance tail
	 pointer to its next */
      next = load(&tail->next);
      if (last_bit(next)) {
	/* tail already dequeued. */
	/* advance head and then tail */
	cas(&q->head, tail, clear_last_bit(next));
	cas(&q->tail, tail, clear_last_bit(next));
	continue;
      }
      /* advance tail pointer */
      cas(&q->tail, tail, next);
    }
  }
}

static inline myth_sleep_queue_item_t myth_sleep_queue_deq(myth_sleep_queue_t * q) {
  while (1) {
    /* check head pointer */
    myth_sleep_queue_item_t head = load(&q->head);
    /* empty */
    if (!head) return 0;
    if (last_bit(head)) continue;
    myth_sleep_queue_item_t head_ = set_last_bit(head);
    if (!cas(&q->head, head, head_)) continue;

    /* check if head has been deleted */
    myth_sleep_queue_item_t next = load(&head->next);
    if (last_bit(next)) {
      /* yes, advance head pointer. if tail 
	 points to the same node, advance tail also */
      int x = cas(&q->head, head_, clear_last_bit(next));
      assert(x);
      cas(&q->tail, head, clear_last_bit(next));
      continue;
    }
    /* it has not been deleted; try to be the one
       who deletes it */
    if (cas(&head->next, next, set_last_bit(next))) {
      /* I won. try to swing the head pointer.
	 enqueuer may have helped this thread by
	 advancing the head */
      int x = cas(&q->head, head_, next);
      assert(x);
      cas(&q->tail, head, next);
      return head;
    }
    int x = cas(&q->head, head_, head);
    assert(x);
  }
}

#endif	/* MYTH_SLEEP_QUEUE_LOCK */

#endif	/* MYTH_SLEEP_QUEUE_FUNC_H_ */