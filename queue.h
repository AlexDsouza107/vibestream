/* queue.h - QUEUE family: circular queue, priority queue, deque, linked-list queue. (Module 2) */
#ifndef QUEUE_H
#define QUEUE_H

#include "song.h"

#define QUEUE_MAX 10

/* ---- Circular queue (array): front/rear wrap around with % ---- */
typedef struct {
    Song data[QUEUE_MAX];
    int front;    /* index of the next song to leave */
    int rear;     /* index where the next song will be stored */
    int count;
} CircularQueue;

void cq_init(CircularQueue *q);
int  cq_isEmpty(const CircularQueue *q);
int  cq_isFull(const CircularQueue *q);
int  cq_enqueue(CircularQueue *q, Song s);
int  cq_dequeue(CircularQueue *q, Song *out);
int  cq_size(const CircularQueue *q);
void cq_display(const CircularQueue *q);

/* ---- Priority queue: higher priority leaves first, FIFO among equals ---- */
#define PRIO_LOW  1    /* normal "Add to queue" */
#define PRIO_HIGH 2    /* "Play Next" */

typedef struct {
    Song song;
    int priority;
} PQEntry;

typedef struct {
    PQEntry data[QUEUE_MAX];   /* kept sorted: index 0 leaves first */
    int count;
} PriorityQueue;

void pq_init(PriorityQueue *q);
int  pq_isEmpty(const PriorityQueue *q);
int  pq_isFull(const PriorityQueue *q);
int  pq_enqueue(PriorityQueue *q, Song s, int priority);
int  pq_dequeue(PriorityQueue *q, Song *out);
void pq_display(const PriorityQueue *q);

/* ---- Deque (double-ended queue): insert/delete at both ends ---- */
typedef struct {
    Song data[QUEUE_MAX];
    int front;
    int count;
} Deque;

void dq_init(Deque *d);
int  dq_isEmpty(const Deque *d);
int  dq_isFull(const Deque *d);
int  dq_insertFront(Deque *d, Song s);
int  dq_insertRear(Deque *d, Song s);
int  dq_deleteFront(Deque *d, Song *out);
int  dq_deleteRear(Deque *d, Song *out);
void dq_display(const Deque *d);

/* ---- Linked-list queue: front/rear pointers, no size limit ---- */
typedef struct QNode {
    Song song;
    struct QNode *next;
} QNode;

typedef struct {
    QNode *front;
    QNode *rear;
    int count;
} LinkedQueue;

void lq_init(LinkedQueue *q);
int  lq_isEmpty(const LinkedQueue *q);
int  lq_enqueue(LinkedQueue *q, Song s);
int  lq_dequeue(LinkedQueue *q, Song *out);
int  lq_size(const LinkedQueue *q);
void lq_display(const LinkedQueue *q);
void lq_free(LinkedQueue *q);

#endif
