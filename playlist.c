/* playlist.c - DOUBLY LINKED LIST + CIRCULAR LINKED LIST (repeat-all mode). */
#include <stdio.h>
#include <stdlib.h>
#include "playlist.h"

void pl_init(Playlist *p) {
    p->head = p->tail = p->current = NULL;
    p->count = 0;
    p->repeat = 0;
}

/* Circular mode links tail->next to head and head->prev to tail.
 * To keep insert/delete code simple (it can rely on NULL ends), we break the
 * circle first and re-join it afterwards. */
static void break_circle(Playlist *p) {
    if (p->repeat && p->head) {
        p->tail->next = NULL;
        p->head->prev = NULL;
    }
}

static void join_circle(Playlist *p) {
    if (p->repeat && p->head) {
        p->tail->next = p->head;
        p->head->prev = p->tail;
    }
}

/* Allocate one node with malloc. */
static PNode *new_node(Song s) {
    PNode *n = malloc(sizeof(PNode));
    if (!n) return NULL;
    n->song = s;
    n->prev = n->next = NULL;
    return n;
}

/* DOUBLY LINKED LIST: insert at any position (1 = front, count+1 = end). */
int pl_insert_at(Playlist *p, int pos, Song s) {
    if (pos < 1 || pos > p->count + 1) return 0;
    PNode *n = new_node(s);
    if (!n) return 0;

    break_circle(p);
    if (!p->head) {                       /* empty list */
        p->head = p->tail = n;
    } else if (pos == 1) {                /* insert at front */
        n->next = p->head;
        p->head->prev = n;
        p->head = n;
    } else if (pos == p->count + 1) {     /* insert at end */
        n->prev = p->tail;
        p->tail->next = n;
        p->tail = n;
    } else {                              /* insert in the middle, before node #pos */
        PNode *c = p->head;
        for (int i = 1; i < pos; i++) c = c->next;
        n->prev = c->prev;
        n->next = c;
        c->prev->next = n;
        c->prev = n;
    }
    p->count++;
    join_circle(p);
    return 1;
}

int pl_insert_end(Playlist *p, Song s) {
    return pl_insert_at(p, p->count + 1, s);
}

/* DOUBLY LINKED LIST: delete by song id, fixing both prev and next links. */
int pl_delete(Playlist *p, int id) {
    break_circle(p);
    PNode *c = p->head;
    while (c && c->song.id != id) c = c->next;
    if (!c) {
        join_circle(p);
        return 0;
    }
    if (c->prev) c->prev->next = c->next; else p->head = c->next;
    if (c->next) c->next->prev = c->prev; else p->tail = c->prev;
    if (p->current == c) p->current = c->prev;   /* keep the cursor valid */
    free(c);
    p->count--;
    join_circle(p);
    return 1;
}

const Song *pl_find(const Playlist *p, int id) {
    PNode *c = p->head;
    for (int i = 0; i < p->count; i++, c = c->next)   /* count-based: safe if circular */
        if (c->song.id == id) return &c->song;
    return NULL;
}

/* Traverse forward using next pointers. */
void pl_display_forward(const Playlist *p) {
    if (!p->head) { printf("  (playlist is empty)\n"); return; }
    PNode *c = p->head;
    for (int i = 0; i < p->count; i++, c = c->next) {
        printf("  %s ", c == p->current ? ">" : " ");
        song_print(&c->song);
        printf("\n");
    }
    if (p->repeat) printf("  (circular: last song links back to the first)\n");
}

/* Traverse backward using prev pointers - only possible because the list is doubly linked. */
void pl_display_backward(const Playlist *p) {
    if (!p->tail) { printf("  (playlist is empty)\n"); return; }
    PNode *c = p->tail;
    for (int i = 0; i < p->count; i++, c = c->prev) {
        printf("  %s ", c == p->current ? ">" : " ");
        song_print(&c->song);
        printf("\n");
    }
}

/* Move the cursor one step forward. */
const Song *pl_next(Playlist *p) {
    PNode *n = p->current ? p->current->next : p->head;
    if (!n) return NULL;
    p->current = n;
    return &n->song;
}

/* Move the cursor one step backward. */
const Song *pl_previous(Playlist *p) {
    PNode *n = p->current ? p->current->prev : NULL;
    if (!n) return NULL;
    p->current = n;
    return &n->song;
}

void pl_reset_cursor(Playlist *p) {
    p->current = NULL;
}

/* CIRCULAR LINKED LIST: turn repeat-all on (link last to first) or off (unlink). */
void pl_set_repeat(Playlist *p, int on) {
    break_circle(p);
    p->repeat = on;
    join_circle(p);
}

void pl_free(Playlist *p) {
    break_circle(p);
    PNode *c = p->head;
    while (c) {
        PNode *next = c->next;
        free(c);
        c = next;
    }
    pl_init(p);
}
