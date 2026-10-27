/* liked.h - Liked songs as a SINGLY LINKED LIST. (Module 3) */
#ifndef LIKED_H
#define LIKED_H

#include "song.h"

typedef struct LikedNode {
    Song song;
    struct LikedNode *next;   /* only a forward link */
} LikedNode;

typedef struct {
    LikedNode *head;
    LikedNode *current;   /* play cursor (NULL = nothing played yet) */
    int count;
} LikedList;

void liked_init(LikedList *l);
int  liked_contains(const LikedList *l, int id);
int  liked_add(LikedList *l, Song s);       /* 0 if duplicate or out of memory */
int  liked_remove(LikedList *l, int id);    /* 0 if not found */
void liked_display(const LikedList *l);
const Song *liked_next(LikedList *l);       /* advance the cursor; NULL at the end */
void liked_reset_cursor(LikedList *l);
void liked_free(LikedList *l);

#endif
