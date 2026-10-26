/* playlist.h - Playlist as a DOUBLY LINKED LIST with optional CIRCULAR repeat mode. (Module 3) */
#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "song.h"

typedef struct PNode {
    Song song;
    struct PNode *prev;   /* link to the previous song */
    struct PNode *next;   /* link to the next song */
} PNode;

typedef struct {
    PNode *head;      /* first song */
    PNode *tail;      /* last song */
    PNode *current;   /* cursor used by next/previous (NULL = nothing played yet) */
    int count;
    int repeat;       /* 1 = circular (tail->next == head), 0 = plain doubly linked list */
} Playlist;

void pl_init(Playlist *p);
int  pl_insert_end(Playlist *p, Song s);
int  pl_insert_at(Playlist *p, int pos, Song s);   /* pos is 1-based; returns 1 on success */
int  pl_delete(Playlist *p, int id);               /* returns 1 if found and deleted */
const Song *pl_find(const Playlist *p, int id);
void pl_display_forward(const Playlist *p);
void pl_display_backward(const Playlist *p);
const Song *pl_next(Playlist *p);                  /* NULL at the end (unless repeat is on) */
const Song *pl_previous(Playlist *p);              /* NULL at the start (unless repeat is on) */
void pl_reset_cursor(Playlist *p);
void pl_set_repeat(Playlist *p, int on);           /* circular linking on/off */
void pl_free(Playlist *p);

#endif
