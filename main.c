/* main.c - VibeStream: menu-driven console music player.
 * Demonstrates Module 2 (Stack, Queue) and Module 3 (Linked List). No audio: "playing" just prints. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "song.h"
#include "playlist.h"
#include "liked.h"
#include "stack.h"
#include "queue.h"

/* All program state lives here. */
typedef struct {
    Playlist playlist;      /* doubly linked list (+ circular repeat) */
    LikedList liked;        /* singly linked list */
    ArrayStack histA;       /* history, array mode */
    LinkedStack histL;      /* history, linked mode */
    CircularQueue queueA;   /* play queue, array mode */
    LinkedQueue queueL;     /* play queue, linked mode */
    PriorityQueue pq;       /* "Play Next" */
    Deque deque;            /* add to front / rear */
    int dynamic;            /* 0 = array versions, 1 = linked-list versions */
    int nextId;
} App;

static App app;

/* ------------------------------------------------------------------ */
/* Input helpers                                                       */
/* ------------------------------------------------------------------ */

static void cleanup(void) {
    pl_free(&app.playlist);
    liked_free(&app.liked);
    lstack_free(&app.histL);
    lq_free(&app.queueL);
}

/* Read one line safely; discards any overflow so the buffer never keeps stale input. */
static void read_line(const char *prompt, char *buf, size_t size) {
    for (;;) {
        printf("%s", prompt);
        fflush(stdout);
        if (!fgets(buf, (int)size, stdin)) {      /* EOF: leave cleanly */
            printf("\n");
            cleanup();
            exit(0);
        }
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') {
            buf[--len] = '\0';
        } else {
            int ch;                               /* line was too long: clear the rest */
            while ((ch = getchar()) != '\n' && ch != EOF) { }
        }
        if (len > 0) return;
        printf("  Input cannot be empty.\n");
    }
}

/* Read an integer in [lo, hi]; keeps asking until valid. */
static int read_int(const char *prompt, int lo, int hi) {
    char buf[64];
    for (;;) {
        read_line(prompt, buf, sizeof buf);
        char *end;
        long v = strtol(buf, &end, 10);
        while (*end == ' ' || *end == '\t') end++;
        if (*end == '\0' && v >= lo && v <= hi) return (int)v;
        printf("  Please enter a number from %d to %d.\n", lo, hi);
    }
}

static Song make_song(int id, const char *title, const char *artist, int duration) {
    Song s;
    s.id = id;
    snprintf(s.title, sizeof s.title, "%s", title);
    snprintf(s.artist, sizeof s.artist, "%s", artist);
    s.duration = duration;
    return s;
}

/* Ask the user to pick a song from the playlist by id. Returns 1 on success. */
static int pick_song(Song *out) {
    pl_display_forward(&app.playlist);
    if (app.playlist.count == 0) return 0;
    int id = read_int("Song id: ", 1, 1000000);
    const Song *s = pl_find(&app.playlist, id);
    if (!s) { printf("  No song with id %d in the playlist.\n", id); return 0; }
    *out = *s;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Mode-aware wrappers: array version or linked version                */
/* ------------------------------------------------------------------ */

static void hist_record(Song s) {
    if (app.dynamic) {
        lstack_push(&app.histL, s);
    } else {
        if (astack_isFull(&app.histA)) astack_drop_oldest(&app.histA);  /* keep newest 10 */
        astack_push(&app.histA, s);
    }
}

static int hist_size(void) {
    return app.dynamic ? lstack_size(&app.histL) : astack_size(&app.histA);
}
static int hist_pop(Song *s)  { return app.dynamic ? lstack_pop(&app.histL, s)  : astack_pop(&app.histA, s); }
static int hist_peek(Song *s) { return app.dynamic ? lstack_peek(&app.histL, s) : astack_peek(&app.histA, s); }

static void hist_display(void) {
    if (app.dynamic) lstack_display(&app.histL); else astack_display(&app.histA);
}

static int queue_add(Song s) {
    return app.dynamic ? lq_enqueue(&app.queueL, s) : cq_enqueue(&app.queueA, s);
}
static int queue_remove(Song *s) {
    return app.dynamic ? lq_dequeue(&app.queueL, s) : cq_dequeue(&app.queueA, s);
}
static void queue_display(void) {
    if (app.dynamic) lq_display(&app.queueL); else cq_display(&app.queueA);
}

/* Switch between array and linked versions, carrying the data across. */
static void toggle_mode(void) {
    Song s;
    if (!app.dynamic) {
        /* array -> linked. Reverse through a temp stack so order is preserved. */
        LinkedStack tmp;
        lstack_init(&tmp);
        while (astack_pop(&app.histA, &s)) lstack_push(&tmp, s);
        while (lstack_pop(&tmp, &s)) lstack_push(&app.histL, s);
        while (cq_dequeue(&app.queueA, &s)) lq_enqueue(&app.queueL, s);
        app.dynamic = 1;
    } else {
        /* linked -> array. Keep only what fits (newest history, first 10 queued). */
        LinkedStack tmp;
        lstack_init(&tmp);
        int skip = lstack_size(&app.histL) - STACK_MAX;
        while (lstack_pop(&app.histL, &s)) lstack_push(&tmp, s);
        while (lstack_pop(&tmp, &s)) {
            if (skip-- > 0) continue;
            astack_push(&app.histA, s);
        }
        int lost = 0;
        while (lq_dequeue(&app.queueL, &s))
            if (!cq_enqueue(&app.queueA, s)) lost++;
        if (lost) printf("  %d queued song(s) did not fit in the array queue and were dropped.\n", lost);
        app.dynamic = 0;
    }
    printf("Mode is now: %s versions of history stack and play queue.\n",
           app.dynamic ? "LINKED LIST (dynamic)" : "ARRAY (fixed size 10)");
}

/* ------------------------------------------------------------------ */
/* Playback logic                                                      */
/* ------------------------------------------------------------------ */

/* "Playing" = print, and push onto the history STACK (the top of history is the current song). */
static void play_song(const Song *s) {
    printf("Now playing: %s - %s\n", s->title, s->artist);
    hist_record(*s);
}

/* Decide what plays next. Order:
 *   1. Priority queue (Play Next = high priority first, then low)
 *   2. Deque (front to rear)
 *   3. Normal play queue (circular queue / linked queue)
 *   4. The playlist (next node of the doubly linked list)  */
static void do_play(void) {
    Song s;
    if (pq_dequeue(&app.pq, &s)) {
        printf("(from priority queue)\n");
        play_song(&s);
    } else if (dq_deleteFront(&app.deque, &s)) {
        printf("(from deque)\n");
        play_song(&s);
    } else if (queue_remove(&s)) {
        printf("(from play queue)\n");
        play_song(&s);
    } else {
        const Song *n = pl_next(&app.playlist);
        if (n) {
            play_song(n);
        } else if (app.playlist.count == 0) {
            printf("Nothing to play: playlist is empty.\n");
        } else {
            pl_reset_cursor(&app.playlist);
            printf("End of playlist. Press Play again to restart (or turn on Repeat All).\n");
        }
    }
}

/* "Previous" button: the top of the history stack is the current song, so pop it
 * and then peek at the one underneath, which becomes the current song. */
static void do_previous(void) {
    if (hist_size() < 2) {
        printf("No previous song in history.\n");
        return;
    }
    Song cur, prev;
    hist_pop(&cur);
    hist_peek(&prev);
    printf("Now playing: %s - %s\n", prev.title, prev.artist);
}

/* ------------------------------------------------------------------ */
/* Show everything                                                     */
/* ------------------------------------------------------------------ */

static void show_all(void) {
    const char *m = app.dynamic ? "linked list" : "array";
    printf("\n=== PLAYLIST (doubly linked list%s) - forward ===\n",
           app.playlist.repeat ? ", CIRCULAR repeat ON" : "");
    pl_display_forward(&app.playlist);
    printf("\n=== LIKED SONGS (singly linked list) ===\n");
    liked_display(&app.liked);
    printf("\n=== HISTORY (%s stack) ===\n", m);
    hist_display();
    printf("\n=== PLAY QUEUE (%s) ===\n", app.dynamic ? "linked queue" : "circular queue");
    queue_display();
    printf("\n=== PLAY NEXT (priority queue) ===\n");
    pq_display(&app.pq);
    printf("\n=== DEQUE ===\n");
    dq_display(&app.deque);
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* Sub-menus                                                           */
/* ------------------------------------------------------------------ */

static void playlist_menu(void) {
    for (;;) {
        printf("\n--- Playlist (doubly linked list) ---\n"
               "1. Add song at end\n2. Insert song at position\n3. Delete song\n"
               "4. Display forward\n5. Display backward\n6. Next song\n7. Previous song\n0. Back\n");
        int c = read_int("Choice: ", 0, 7);
        if (c == 0) return;
        if (c == 1 || c == 2) {
            char title[50], artist[50];
            read_line("Title: ", title, sizeof title);
            read_line("Artist: ", artist, sizeof artist);
            int dur = read_int("Duration in seconds (1-3600): ", 1, 3600);
            Song s = make_song(app.nextId, title, artist, dur);
            int ok;
            if (c == 1) {
                ok = pl_insert_end(&app.playlist, s);
            } else {
                int pos = read_int("Position: ", 1, app.playlist.count + 1);
                ok = pl_insert_at(&app.playlist, pos, s);
            }
            if (ok) { printf("Added with id %d.\n", app.nextId); app.nextId++; }
            else printf("Could not add song.\n");
        } else if (c == 3) {
            if (app.playlist.count == 0) { printf("Playlist is empty.\n"); continue; }
            pl_display_forward(&app.playlist);
            int id = read_int("Id to delete: ", 1, 1000000);
            if (pl_delete(&app.playlist, id)) {
                liked_remove(&app.liked, id);
                printf("Deleted.\n");
            } else printf("No song with id %d.\n", id);
        } else if (c == 4) {
            pl_display_forward(&app.playlist);
        } else if (c == 5) {
            pl_display_backward(&app.playlist);
        } else if (c == 6) {
            const Song *s = pl_next(&app.playlist);
            if (s) play_song(s); else printf("Already at the last song (turn on Repeat All to wrap).\n");
        } else {
            const Song *s = pl_previous(&app.playlist);
            if (s) play_song(s); else printf("Already at the first song.\n");
        }
    }
}

static void liked_menu(void) {
    for (;;) {
        printf("\n--- Liked songs (singly linked list) ---\n"
               "1. Like a song\n2. Unlike a song\n3. Display liked songs\n0. Back\n");
        int c = read_int("Choice: ", 0, 3);
        if (c == 0) return;
        if (c == 1) {
            Song s;
            if (!pick_song(&s)) continue;
            if (liked_add(&app.liked, s)) printf("Liked: %s\n", s.title);
            else printf("Already liked.\n");
        } else if (c == 2) {
            if (app.liked.count == 0) { printf("No liked songs.\n"); continue; }
            liked_display(&app.liked);
            int id = read_int("Id to remove: ", 1, 1000000);
            printf(liked_remove(&app.liked, id) ? "Removed.\n" : "That song is not in liked songs.\n");
        } else {
            liked_display(&app.liked);
        }
    }
}

static void queue_menu(void) {
    for (;;) {
        printf("\n--- Queues (%s mode) ---\n"
               "1. Add to queue (%s)\n2. Remove front of queue (without playing)\n"
               "3. Play Next (priority queue, HIGH)\n4. Add to priority queue (LOW)\n"
               "5. Deque: insert front\n6. Deque: insert rear\n"
               "7. Deque: delete front\n8. Deque: delete rear\n"
               "9. Display all queues\n0. Back\n",
               app.dynamic ? "linked" : "array",
               app.dynamic ? "linked queue" : "circular queue");
        int c = read_int("Choice: ", 0, 9);
        Song s;
        if (c == 0) return;
        if (c == 9) {
            printf("Play queue:\n");   queue_display();
            printf("Priority queue:\n"); pq_display(&app.pq);
            printf("Deque:\n");        dq_display(&app.deque);
        } else if (c == 2) {
            if (queue_remove(&s)) { printf("Removed: "); song_print(&s); printf("\n"); }
            else printf("Queue is empty.\n");
        } else if (c == 7) {
            if (dq_deleteFront(&app.deque, &s)) { printf("Removed: "); song_print(&s); printf("\n"); }
            else printf("Deque is empty.\n");
        } else if (c == 8) {
            if (dq_deleteRear(&app.deque, &s)) { printf("Removed: "); song_print(&s); printf("\n"); }
            else printf("Deque is empty.\n");
        } else {
            if (!pick_song(&s)) continue;
            int ok = 0;
            if (c == 1) ok = queue_add(s);
            else if (c == 3) ok = pq_enqueue(&app.pq, s, PRIO_HIGH);
            else if (c == 4) ok = pq_enqueue(&app.pq, s, PRIO_LOW);
            else if (c == 5) ok = dq_insertFront(&app.deque, s);
            else ok = dq_insertRear(&app.deque, s);
            printf(ok ? "Added: %s\n" : "That queue is full! (%s not added)\n", s.title);
        }
    }
}

/* ------------------------------------------------------------------ */

static void init_app(void) {
    memset(&app, 0, sizeof app);
    pl_init(&app.playlist);
    liked_init(&app.liked);
    astack_init(&app.histA);
    lstack_init(&app.histL);
    cq_init(&app.queueA);
    lq_init(&app.queueL);
    pq_init(&app.pq);
    dq_init(&app.deque);

    /* Preload 8 sample songs. */
    static const struct { const char *t, *a; int d; } sample[8] = {
        {"Blinding Lights", "The Weeknd", 200}, {"Shape of You", "Ed Sheeran", 234},
        {"Believer", "Imagine Dragons", 204},   {"Levitating", "Dua Lipa", 203},
        {"Starboy", "The Weeknd", 230},         {"Perfect", "Ed Sheeran", 263},
        {"Senorita", "Shawn Mendes", 191},      {"Counting Stars", "OneRepublic", 257}
    };
    for (int i = 0; i < 8; i++)
        pl_insert_end(&app.playlist, make_song(i + 1, sample[i].t, sample[i].a, sample[i].d));
    app.nextId = 9;
}

int main(void) {
    init_app();
    printf("=== VibeStream ===\n");
    for (;;) {
        printf("\n===== MAIN MENU  [mode: %s | repeat: %s] =====\n"
               "1. Play\n2. Previous\n3. Playlist\n4. Liked songs\n5. Queues\n"
               "6. Toggle Repeat All (circular list)\n"
               "7. Toggle array / linked-list mode\n"
               "8. Show all structures\n0. Exit\n",
               app.dynamic ? "linked" : "array", app.playlist.repeat ? "ON" : "OFF");
        int c = read_int("Choice: ", 0, 8);
        if (c == 0) break;
        switch (c) {
            case 1: do_play(); break;
            case 2: do_previous(); break;
            case 3: playlist_menu(); break;
            case 4: liked_menu(); break;
            case 5: queue_menu(); break;
            case 6:
                pl_set_repeat(&app.playlist, !app.playlist.repeat);
                printf("Repeat All is now %s.\n", app.playlist.repeat ? "ON (circular)" : "OFF");
                break;
            case 7: toggle_mode(); break;
            case 8: show_all(); break;
        }
    }
    cleanup();
    printf("Goodbye!\n");
    return 0;
}
