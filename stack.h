/* stack.h - STACK two ways: fixed array (MAX 10) and dynamic linked list. (Module 2) */
#ifndef STACK_H
#define STACK_H

#include "song.h"

#define STACK_MAX 10

/* ---- Array stack: top is an index; LIFO ---- */
typedef struct {
    Song data[STACK_MAX];
    int top;              /* index of the top element, -1 when empty */
} ArrayStack;

void astack_init(ArrayStack *s);
int  astack_isEmpty(const ArrayStack *s);
int  astack_isFull(const ArrayStack *s);
int  astack_push(ArrayStack *s, Song song);        /* 0 if full */
int  astack_pop(ArrayStack *s, Song *out);         /* 0 if empty */
int  astack_peek(const ArrayStack *s, Song *out);  /* 0 if empty */
int  astack_size(const ArrayStack *s);
void astack_drop_oldest(ArrayStack *s);            /* helper: remove the bottom element */
void astack_display(const ArrayStack *s);

/* ---- Linked-list stack: top is a pointer; no size limit ---- */
typedef struct StackNode {
    Song song;
    struct StackNode *next;
} StackNode;

typedef struct {
    StackNode *top;
    int count;
} LinkedStack;

void lstack_init(LinkedStack *s);
int  lstack_isEmpty(const LinkedStack *s);
int  lstack_push(LinkedStack *s, Song song);       /* 0 only if malloc fails */
int  lstack_pop(LinkedStack *s, Song *out);
int  lstack_peek(const LinkedStack *s, Song *out);
int  lstack_size(const LinkedStack *s);
void lstack_display(const LinkedStack *s);
void lstack_free(LinkedStack *s);

#endif
