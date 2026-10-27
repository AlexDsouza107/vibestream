/* liked.c - SINGLY LINKED LIST of liked songs. */
#include <stdio.h>
#include <stdlib.h>
#include "liked.h"

void liked_init(LikedList *l) { l->head = l->current = NULL; l->count = 0; }

int liked_contains(const LikedList *l, int id) {
    for (LikedNode *c = l->head; c; c = c->next)
        if (c->song.id == id) return 1;
    return 0;
}

/* Insert at the end: walk to the last node, then link the new node after it. */
int liked_add(LikedList *l, Song s) {
    if (liked_contains(l, s.id)) return 0;
    LikedNode *n = malloc(sizeof(LikedNode));
    if (!n) return 0;
    n->song = s;
    n->next = NULL;
    if (!l->head) {
        l->head = n;
    } else {
        LikedNode *c = l->head;
        while (c->next) c = c->next;
        c->next = n;
    }
    l->count++;
    return 1;
}

/* Delete by id: keep a 'prev' pointer so we can bypass the removed node. */
int liked_remove(LikedList *l, int id) {
    LikedNode *prev = NULL, *c = l->head;
    while (c && c->song.id != id) {
        prev = c;
        c = c->next;
    }
    if (!c) return 0;
    if (prev) prev->next = c->next; else l->head = c->next;
    if (l->current == c) l->current = prev;       /* keep the cursor valid */
    free(c);
    l->count--;
    return 1;
}

void liked_display(const LikedList *l) {
    if (!l->head) { printf("  (no liked songs)\n"); return; }
    int i = 1;
    for (LikedNode *c = l->head; c; c = c->next, i++) {
        printf("  %d. ", i);
        song_print(&c->song);
        printf("\n");
    }
}

/* Singly linked list traversal: move the cursor one node forward. */
const Song *liked_next(LikedList *l) {
    LikedNode *n = l->current ? l->current->next : l->head;
    if (!n) return NULL;
    l->current = n;
    return &n->song;
}

void liked_reset_cursor(LikedList *l) { l->current = NULL; }

void liked_free(LikedList *l) {
    LikedNode *c = l->head;
    while (c) {
        LikedNode *next = c->next;
        free(c);
        c = next;
    }
    liked_init(l);
}
