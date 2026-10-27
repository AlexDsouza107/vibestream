/* queue.c - QUEUE (FIFO) variants. */
#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

/* ================= CIRCULAR QUEUE ================= */

void cq_init(CircularQueue *q) { q->front = q->rear = q->count = 0; }

int cq_isEmpty(const CircularQueue *q) { return q->count == 0; }

int cq_isFull(const CircularQueue *q) { return q->count == QUEUE_MAX; }

/* enqueue at rear; rear wraps to 0 after the last slot so freed space is reused. */
int cq_enqueue(CircularQueue *q, Song s) {
    if (cq_isFull(q)) return 0;
    q->data[q->rear] = s;
    q->rear = (q->rear + 1) % QUEUE_MAX;
    q->count++;
    return 1;
}

/* dequeue from front; front also wraps around. */
int cq_dequeue(CircularQueue *q, Song *out) {
    if (cq_isEmpty(q)) return 0;
    *out = q->data[q->front];
    q->front = (q->front + 1) % QUEUE_MAX;
    q->count--;
    return 1;
}

int cq_size(const CircularQueue *q) { return q->count; }

/* Remove the i-th element (0 = front): close the gap by shifting later elements forward. */
int cq_removeAt(CircularQueue *q, int i, Song *out) {
    if (i < 0 || i >= q->count) return 0;
    *out = q->data[(q->front + i) % QUEUE_MAX];
    for (int j = i; j < q->count - 1; j++)
        q->data[(q->front + j) % QUEUE_MAX] = q->data[(q->front + j + 1) % QUEUE_MAX];
    q->rear = (q->rear - 1 + QUEUE_MAX) % QUEUE_MAX;
    q->count--;
    return 1;
}

void cq_display(const CircularQueue *q) {
    if (cq_isEmpty(q)) { printf("  (empty)\n"); return; }
    for (int i = 0; i < q->count; i++) {
        printf("  %s ", i == 0 ? "FRONT>" : "      ");
        song_print(&q->data[(q->front + i) % QUEUE_MAX]);
        printf("\n");
    }
    printf("  (%d/%d used, front=%d rear=%d)\n", q->count, QUEUE_MAX, q->front, q->rear);
}

/* ================= PRIORITY QUEUE ================= */

void pq_init(PriorityQueue *q) { q->count = 0; }

int pq_isEmpty(const PriorityQueue *q) { return q->count == 0; }

int pq_isFull(const PriorityQueue *q) { return q->count == QUEUE_MAX; }

/* enqueue: insert so the array stays ordered by priority (high first).
 * The new entry goes AFTER existing entries of the same priority, so equals stay FIFO. */
int pq_enqueue(PriorityQueue *q, Song s, int priority) {
    if (pq_isFull(q)) return 0;
    int i = q->count - 1;
    while (i >= 0 && q->data[i].priority < priority) {
        q->data[i + 1] = q->data[i];   /* shift lower-priority entries back */
        i--;
    }
    q->data[i + 1].song = s;
    q->data[i + 1].priority = priority;
    q->count++;
    return 1;
}

/* dequeue: the highest priority entry is always at index 0. */
int pq_dequeue(PriorityQueue *q, Song *out) {
    if (pq_isEmpty(q)) return 0;
    *out = q->data[0].song;
    for (int i = 1; i < q->count; i++) q->data[i - 1] = q->data[i];
    q->count--;
    return 1;
}

int pq_removeAt(PriorityQueue *q, int i, Song *out) {
    if (i < 0 || i >= q->count) return 0;
    *out = q->data[i].song;
    for (int j = i + 1; j < q->count; j++) q->data[j - 1] = q->data[j];
    q->count--;
    return 1;
}

void pq_display(const PriorityQueue *q) {
    if (pq_isEmpty(q)) { printf("  (empty)\n"); return; }
    for (int i = 0; i < q->count; i++) {
        printf("  %-5s ", q->data[i].priority == PRIO_HIGH ? "HIGH" : "LOW");
        song_print(&q->data[i].song);
        printf("\n");
    }
}

/* ================= DEQUE ================= */

void dq_init(Deque *d) { d->front = d->count = 0; }

int dq_isEmpty(const Deque *d) { return d->count == 0; }

int dq_isFull(const Deque *d) { return d->count == QUEUE_MAX; }

/* insert front: move front one step BACK (wrapping), then store. */
int dq_insertFront(Deque *d, Song s) {
    if (dq_isFull(d)) return 0;
    d->front = (d->front - 1 + QUEUE_MAX) % QUEUE_MAX;
    d->data[d->front] = s;
    d->count++;
    return 1;
}

/* insert rear: store just past the last element. */
int dq_insertRear(Deque *d, Song s) {
    if (dq_isFull(d)) return 0;
    d->data[(d->front + d->count) % QUEUE_MAX] = s;
    d->count++;
    return 1;
}

int dq_deleteFront(Deque *d, Song *out) {
    if (dq_isEmpty(d)) return 0;
    *out = d->data[d->front];
    d->front = (d->front + 1) % QUEUE_MAX;
    d->count--;
    return 1;
}

int dq_deleteRear(Deque *d, Song *out) {
    if (dq_isEmpty(d)) return 0;
    *out = d->data[(d->front + d->count - 1) % QUEUE_MAX];
    d->count--;
    return 1;
}

int dq_removeAt(Deque *d, int i, Song *out) {
    if (i < 0 || i >= d->count) return 0;
    *out = d->data[(d->front + i) % QUEUE_MAX];
    for (int j = i; j < d->count - 1; j++)
        d->data[(d->front + j) % QUEUE_MAX] = d->data[(d->front + j + 1) % QUEUE_MAX];
    d->count--;
    return 1;
}

void dq_display(const Deque *d) {
    if (dq_isEmpty(d)) { printf("  (empty)\n"); return; }
    for (int i = 0; i < d->count; i++) {
        printf("  %s ", i == 0 ? "FRONT>" : (i == d->count - 1 ? "REAR> " : "      "));
        song_print(&d->data[(d->front + i) % QUEUE_MAX]);
        printf("\n");
    }
}

/* ================= LINKED-LIST QUEUE ================= */

void lq_init(LinkedQueue *q) { q->front = q->rear = NULL; q->count = 0; }

int lq_isEmpty(const LinkedQueue *q) { return q->front == NULL; }

/* enqueue: append a node after rear. */
int lq_enqueue(LinkedQueue *q, Song s) {
    QNode *n = malloc(sizeof(QNode));
    if (!n) return 0;
    n->song = s;
    n->next = NULL;
    if (q->rear) q->rear->next = n; else q->front = n;
    q->rear = n;
    q->count++;
    return 1;
}

/* dequeue: remove the node at front. */
int lq_dequeue(LinkedQueue *q, Song *out) {
    if (lq_isEmpty(q)) return 0;
    QNode *n = q->front;
    *out = n->song;
    q->front = n->next;
    if (!q->front) q->rear = NULL;
    free(n);
    q->count--;
    return 1;
}

int lq_size(const LinkedQueue *q) { return q->count; }

/* Linked version: walk with a 'prev' pointer, then unlink the node. */
int lq_removeAt(LinkedQueue *q, int i, Song *out) {
    if (i < 0 || i >= q->count) return 0;
    QNode *prev = NULL, *c = q->front;
    for (int j = 0; j < i; j++) { prev = c; c = c->next; }
    *out = c->song;
    if (prev) prev->next = c->next; else q->front = c->next;
    if (c == q->rear) q->rear = prev;
    free(c);
    q->count--;
    return 1;
}

void lq_display(const LinkedQueue *q) {
    if (lq_isEmpty(q)) { printf("  (empty)\n"); return; }
    for (QNode *c = q->front; c; c = c->next) {
        printf("  %s ", c == q->front ? "FRONT>" : "      ");
        song_print(&c->song);
        printf("\n");
    }
    printf("  (%d songs, no size limit)\n", q->count);
}

void lq_free(LinkedQueue *q) {
    Song tmp;
    while (lq_dequeue(q, &tmp)) { }
}
