/* song.h - the Song record shared by every data structure in the project. */
#ifndef SONG_H
#define SONG_H

#include <stdio.h>

typedef struct {
    int id;
    char title[50];
    char artist[50];
    int duration;   /* length in seconds */
} Song;

/* Print one song on a single line, e.g. "[3] Blinding Lights - The Weeknd (3:20)" */
static inline void song_print(const Song *s) {
    printf("[%d] %s - %s (%d:%02d)", s->id, s->title, s->artist,
           s->duration / 60, s->duration % 60);
}

#endif
