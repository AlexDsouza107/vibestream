/* stack.c - STACK (LIFO) implemented with an array and with a linked list. */
#include <stdio.h>
#include <stdlib.h>
#include "stack.h"

/* ================= ARRAY STACK ================= */

void astack_init(ArrayStack *s) { s->top = -1; }

int astack_isEmpty(const ArrayStack *s) { return s->top == -1; }

int astack_isFull(const ArrayStack *s) { return s->top == STACK_MAX - 1; }

/* push: move top up by one, then store. */
int astack_push(ArrayStack *s, Song song) {
    if (astack_isFull(s)) return 0;
    s->data[++s->top] = song;
    return 1;
}

/* pop: read the top element, then move top down by one. */
int astack_pop(ArrayStack *s, Song *out) {
    if (astack_isEmpty(s)) return 0;
    *out = s->data[s->top--];
    return 1;
}

/* peek: look at the top element without removing it. */
int astack_peek(const ArrayStack *s, Song *out) {
    if (astack_isEmpty(s)) return 0;
    *out = s->data[s->top];
    return 1;
}

int astack_size(const ArrayStack *s) { return s->top + 1; }

/* Not a real stack operation: used so a full history can keep accepting new songs. */
void astack_drop_oldest(ArrayStack *s) {
    for (int i = 1; i <= s->top; i++) s->data[i - 1] = s->data[i];
    if (s->top >= 0) s->top--;
}

void astack_display(const ArrayStack *s) {
    if (astack_isEmpty(s)) { printf("  (empty)\n"); return; }
    for (int i = s->top; i >= 0; i--) {
        printf("  %s ", i == s->top ? "TOP>" : "    ");
        song_print(&s->data[i]);
        printf("\n");
    }
    printf("  (%d/%d used)\n", s->top + 1, STACK_MAX);
}

/* ================= LINKED-LIST STACK ================= */

void lstack_init(LinkedStack *s) { s->top = NULL; s->count = 0; }

int lstack_isEmpty(const LinkedStack *s) { return s->top == NULL; }

/* push: new node becomes the new top (insert at head of a singly linked list). */
int lstack_push(LinkedStack *s, Song song) {
    StackNode *n = malloc(sizeof(StackNode));
    if (!n) return 0;
    n->song = song;
    n->next = s->top;
    s->top = n;
    s->count++;
    return 1;
}

/* pop: unlink the head node, hand back its song, free it. */
int lstack_pop(LinkedStack *s, Song *out) {
    if (lstack_isEmpty(s)) return 0;
    StackNode *n = s->top;
    *out = n->song;
    s->top = n->next;
    free(n);
    s->count--;
    return 1;
}

int lstack_peek(const LinkedStack *s, Song *out) {
    if (lstack_isEmpty(s)) return 0;
    *out = s->top->song;
    return 1;
}

int lstack_size(const LinkedStack *s) { return s->count; }

void lstack_display(const LinkedStack *s) {
    if (lstack_isEmpty(s)) { printf("  (empty)\n"); return; }
    for (StackNode *c = s->top; c; c = c->next) {
        printf("  %s ", c == s->top ? "TOP>" : "    ");
        song_print(&c->song);
        printf("\n");
    }
    printf("  (%d songs, no size limit)\n", s->count);
}

void lstack_free(LinkedStack *s) {
    Song tmp;
    while (lstack_pop(s, &tmp)) { }
}
