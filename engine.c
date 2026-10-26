/* engine.c - Same data structures as the console app, driven by text commands instead of a menu.
 * Reads one command per line from stdin, prints one JSON line (result + full state) to stdout.
 * The web server (server.js) talks to this program; all logic still lives in the C modules. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "song.h"
#include "playlist.h"
#include "liked.h"
#include "stack.h"
#include "queue.h"

static Playlist playlist;
static LikedList liked;
static ArrayStack histA;
static LinkedStack histL;
static CircularQueue queueA;
static LinkedQueue queueL;
static PriorityQueue pq;
static Deque deque;
static int dynamic = 0;
static int nextId = 9;

static int  res_ok;
static char res_msg[200];

static void say(int ok, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res_msg, sizeof res_msg, fmt, ap);
    va_end(ap);
    res_ok = ok;
}

/* ---------- mode-aware helpers (array vs linked) ---------- */

static void hist_record(Song s) {
    if (dynamic) { lstack_push(&histL, s); return; }
    if (astack_isFull(&histA)) astack_drop_oldest(&histA);
    astack_push(&histA, s);
}
static int hist_size(void)    { return dynamic ? lstack_size(&histL) : astack_size(&histA); }
static int hist_pop(Song *s)  { return dynamic ? lstack_pop(&histL, s) : astack_pop(&histA, s); }
static int hist_peek(Song *s) { return dynamic ? lstack_peek(&histL, s) : astack_peek(&histA, s); }
static int queue_add(Song s)  { return dynamic ? lq_enqueue(&queueL, s) : cq_enqueue(&queueA, s); }
static int queue_remove(Song *s) { return dynamic ? lq_dequeue(&queueL, s) : cq_dequeue(&queueA, s); }

static void toggle_mode(void) {
    Song s;
    LinkedStack tmp;
    lstack_init(&tmp);
    if (!dynamic) {
        while (astack_pop(&histA, &s)) lstack_push(&tmp, s);
        while (lstack_pop(&tmp, &s)) lstack_push(&histL, s);
        while (cq_dequeue(&queueA, &s)) lq_enqueue(&queueL, s);
        dynamic = 1;
    } else {
        int skip = lstack_size(&histL) - STACK_MAX;
        while (lstack_pop(&histL, &s)) lstack_push(&tmp, s);
        while (lstack_pop(&tmp, &s)) {
            if (skip-- > 0) continue;
            astack_push(&histA, s);
        }
        int lost = 0;
        while (lq_dequeue(&queueL, &s))
            if (!cq_enqueue(&queueA, s)) lost++;
        dynamic = 0;
        if (lost) { say(1, "Switched to array mode; %d queued song(s) did not fit and were dropped.", lost); return; }
    }
    say(1, "Switched to %s versions.", dynamic ? "linked-list (unlimited)" : "array (max 10)");
}

/* ---------- JSON output ---------- */

static void json_str(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c >= 32) putchar(c);
    }
    putchar('"');
}

static void json_song(const Song *s) {
    printf("{\"id\":%d,\"title\":", s->id);
    json_str(s->title);
    printf(",\"artist\":");
    json_str(s->artist);
    printf(",\"duration\":%d}", s->duration);
}

static void json_song_prio(const Song *s, int prio) {
    printf("{\"song\":");
    json_song(s);
    printf(",\"priority\":\"%s\"}", prio == PRIO_HIGH ? "HIGH" : "LOW");
}

static void print_state(void) {
    printf("\"state\":{\"dynamic\":%d,\"repeat\":%d,\"capacity\":%d,", dynamic, playlist.repeat, QUEUE_MAX);

    printf("\"currentId\":%d,\"playlist\":[", playlist.current ? playlist.current->song.id : 0);
    PNode *c = playlist.head;
    for (int i = 0; i < playlist.count; i++, c = c->next) {
        if (i) putchar(',');
        json_song(&c->song);
    }

    printf("],\"liked\":[");
    for (LikedNode *l = liked.head; l; l = l->next) {
        json_song(&l->song);
        if (l->next) putchar(',');
    }

    printf("],\"history\":[");              /* top of the stack first */
    if (dynamic) {
        for (StackNode *n = histL.top; n; n = n->next) {
            json_song(&n->song);
            if (n->next) putchar(',');
        }
    } else {
        for (int i = histA.top; i >= 0; i--) {
            json_song(&histA.data[i]);
            if (i > 0) putchar(',');
        }
    }

    printf("],\"queue\":[");                /* front first */
    if (dynamic) {
        for (QNode *n = queueL.front; n; n = n->next) {
            json_song(&n->song);
            if (n->next) putchar(',');
        }
    } else {
        for (int i = 0; i < queueA.count; i++) {
            if (i) putchar(',');
            json_song(&queueA.data[(queueA.front + i) % QUEUE_MAX]);
        }
    }

    printf("],\"priority\":[");
    for (int i = 0; i < pq.count; i++) {
        if (i) putchar(',');
        json_song_prio(&pq.data[i].song, pq.data[i].priority);
    }

    printf("],\"deque\":[");
    for (int i = 0; i < deque.count; i++) {
        if (i) putchar(',');
        json_song(&deque.data[(deque.front + i) % QUEUE_MAX]);
    }
    printf("]}");
}

static void respond(void) {
    printf("{\"ok\":%s,\"msg\":", res_ok ? "true" : "false");
    json_str(res_msg);
    printf(",");
    print_state();
    printf("}\n");
    fflush(stdout);
}

/* ---------- commands ---------- */

static void trim(char *s) {
    char *a = s;
    while (*a == ' ' || *a == '\t') a++;
    memmove(s, a, strlen(a) + 1);
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
}

/* Parse a whole-string integer in [lo, hi]; returns 1 on success. */
static int to_int(const char *s, int lo, int hi, int *out) {
    if (!s || !*s) return 0;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < lo || v > hi) return 0;
    *out = (int)v;
    return 1;
}

static void play_song(const Song *s, const char *from) {
    hist_record(*s);
    say(1, "Now playing: %s - %s%s", s->title, s->artist, from);
}

static void cmd_play(void) {
    Song s;
    if (pq_dequeue(&pq, &s)) play_song(&s, " (from priority queue)");
    else if (dq_deleteFront(&deque, &s)) play_song(&s, " (from deque)");
    else if (queue_remove(&s)) play_song(&s, " (from play queue)");
    else {
        const Song *n = pl_next(&playlist);
        if (n) play_song(n, "");
        else if (playlist.count == 0) say(0, "Nothing to play: playlist is empty.");
        else { pl_reset_cursor(&playlist); say(0, "End of playlist. Press Play to restart, or turn on Repeat All."); }
    }
}

static void cmd_prev(void) {
    if (hist_size() < 2) { say(0, "No previous song in history."); return; }
    Song cur, prev;
    hist_pop(&cur);
    hist_peek(&prev);
    say(1, "Now playing: %s - %s (previous, popped from history stack)", prev.title, prev.artist);
}

static int build_song(char **f, int nf, int first, Song *out) {
    int dur;
    if (nf < first + 3) { say(0, "Title, artist and duration are required."); return 0; }
    trim(f[first]); trim(f[first + 1]); trim(f[first + 2]);
    if (!f[first][0] || !f[first + 1][0]) { say(0, "Title and artist cannot be empty."); return 0; }
    if (!to_int(f[first + 2], 1, 3600, &dur)) { say(0, "Duration must be 1-3600 seconds."); return 0; }
    out->id = nextId;
    snprintf(out->title, sizeof out->title, "%s", f[first]);
    snprintf(out->artist, sizeof out->artist, "%s", f[first + 1]);
    out->duration = dur;
    return 1;
}

static void handle(char *line) {
    char *f[5] = {0};
    int nf = 0, id, pos;
    Song s;
    trim(line);

    char *sp = strchr(line, ' ');
    char *args = "";
    if (sp) { *sp = '\0'; args = sp + 1; }
    char *cmd = line;
    /* split args on '|' */
    f[nf++] = args;
    for (char *p = args; *p && nf < 5; p++)
        if (*p == '|') { *p = '\0'; f[nf++] = p + 1; }
    for (int i = 0; i < nf; i++) trim(f[i]);

    int needs_id = !strcmp(cmd, "like") || !strcmp(cmd, "unlike") || !strcmp(cmd, "pl_delete") ||
                   !strcmp(cmd, "q_add") || !strcmp(cmd, "playnext") || !strcmp(cmd, "pq_low") ||
                   !strcmp(cmd, "dq_front") || !strcmp(cmd, "dq_rear") || !strcmp(cmd, "play_id");
    if (needs_id && !to_int(f[0], 1, 1000000, &id)) { say(0, "A valid song id is required."); return; }

    if (!strcmp(cmd, "state")) say(1, "ok");
    else if (!strcmp(cmd, "play")) cmd_play();
    else if (!strcmp(cmd, "prev")) cmd_prev();
    else if (!strcmp(cmd, "play_id")) {        /* jump the playlist cursor to a chosen node and play it */
        PNode *n = playlist.head;
        for (int i = 0; i < playlist.count && n; i++, n = n->next) {
            if (n->song.id == id) { playlist.current = n; play_song(&n->song, ""); return; }
        }
        say(0, "No song with id %d.", id);
    }
    else if (!strcmp(cmd, "pl_next")) {
        const Song *n = pl_next(&playlist);
        if (n) play_song(n, " (playlist next)"); else say(0, "Already at the last song. Turn on Repeat All to wrap.");
    } else if (!strcmp(cmd, "pl_prev")) {
        const Song *n = pl_previous(&playlist);
        if (n) play_song(n, " (playlist previous)"); else say(0, "Already at the first song.");
    } else if (!strcmp(cmd, "pl_add")) {
        if (build_song(f, nf, 0, &s)) {
            if (pl_insert_end(&playlist, s)) { nextId++; say(1, "Added \"%s\" at the end (id %d).", s.title, s.id); }
            else say(0, "Could not add song.");
        }
    } else if (!strcmp(cmd, "pl_insert")) {
        if (nf < 4) { say(0, "Position, title, artist and duration are required."); }
        else if (!to_int(f[0], 1, playlist.count + 1, &pos)) say(0, "Position must be 1-%d.", playlist.count + 1);
        else if (build_song(f, nf, 1, &s)) {
            if (pl_insert_at(&playlist, pos, s)) { nextId++; say(1, "Inserted \"%s\" at position %d (id %d).", s.title, pos, s.id); }
            else say(0, "Could not insert song.");
        }
    } else if (!strcmp(cmd, "pl_delete")) {
        if (pl_delete(&playlist, id)) { liked_remove(&liked, id); say(1, "Deleted song %d from the playlist.", id); }
        else say(0, "No song with id %d.", id);
    } else if (!strcmp(cmd, "like") || !strcmp(cmd, "q_add") || !strcmp(cmd, "playnext") ||
               !strcmp(cmd, "pq_low") || !strcmp(cmd, "dq_front") || !strcmp(cmd, "dq_rear")) {
        const Song *p = pl_find(&playlist, id);
        if (!p) { say(0, "No song with id %d in the playlist.", id); return; }
        s = *p;
        int ok;
        const char *where;
        if (!strcmp(cmd, "like"))          { ok = liked_add(&liked, s);             where = "liked songs"; }
        else if (!strcmp(cmd, "q_add"))    { ok = queue_add(s);                     where = "the play queue"; }
        else if (!strcmp(cmd, "playnext")) { ok = pq_enqueue(&pq, s, PRIO_HIGH);    where = "Play Next (priority HIGH)"; }
        else if (!strcmp(cmd, "pq_low"))   { ok = pq_enqueue(&pq, s, PRIO_LOW);     where = "the priority queue (LOW)"; }
        else if (!strcmp(cmd, "dq_front")) { ok = dq_insertFront(&deque, s);        where = "the front of the deque"; }
        else                               { ok = dq_insertRear(&deque, s);         where = "the rear of the deque"; }
        if (ok) say(1, "Added \"%s\" to %s.", s.title, where);
        else if (!strcmp(cmd, "like")) say(0, "\"%s\" is already liked.", s.title);
        else say(0, "Cannot add: that structure is full (max %d).", QUEUE_MAX);
    } else if (!strcmp(cmd, "unlike")) {
        if (liked_remove(&liked, id)) say(1, "Removed from liked songs."); else say(0, "That song is not liked.");
    } else if (!strcmp(cmd, "q_remove")) {
        if (queue_remove(&s)) say(1, "Dequeued \"%s\".", s.title); else say(0, "Play queue is empty.");
    } else if (!strcmp(cmd, "pq_remove")) {
        if (pq_dequeue(&pq, &s)) say(1, "Dequeued \"%s\" from priority queue.", s.title); else say(0, "Priority queue is empty.");
    } else if (!strcmp(cmd, "dq_delfront")) {
        if (dq_deleteFront(&deque, &s)) say(1, "Deleted \"%s\" from deque front.", s.title); else say(0, "Deque is empty.");
    } else if (!strcmp(cmd, "dq_delrear")) {
        if (dq_deleteRear(&deque, &s)) say(1, "Deleted \"%s\" from deque rear.", s.title); else say(0, "Deque is empty.");
    } else if (!strcmp(cmd, "repeat")) {
        pl_set_repeat(&playlist, !playlist.repeat);
        say(1, playlist.repeat ? "Repeat All ON: playlist is now a circular linked list." : "Repeat All OFF: circle unlinked.");
    } else if (!strcmp(cmd, "mode")) {
        toggle_mode();
    } else {
        say(0, "Unknown command.");
    }
}

int main(void) {
    pl_init(&playlist); liked_init(&liked);
    astack_init(&histA); lstack_init(&histL);
    cq_init(&queueA); lq_init(&queueL);
    pq_init(&pq); dq_init(&deque);

    static const struct { const char *t, *a; int d; } sample[8] = {
        {"Blinding Lights", "The Weeknd", 200}, {"Shape of You", "Ed Sheeran", 234},
        {"Believer", "Imagine Dragons", 204},   {"Levitating", "Dua Lipa", 203},
        {"Starboy", "The Weeknd", 230},         {"Perfect", "Ed Sheeran", 263},
        {"Senorita", "Shawn Mendes", 191},      {"Counting Stars", "OneRepublic", 257}
    };
    for (int i = 0; i < 8; i++) {
        Song s;
        s.id = i + 1;
        snprintf(s.title, sizeof s.title, "%s", sample[i].t);
        snprintf(s.artist, sizeof s.artist, "%s", sample[i].a);
        s.duration = sample[i].d;
        pl_insert_end(&playlist, s);
    }

    char line[512];
    say(1, "VibeStream engine ready.");
    respond();
    while (fgets(line, sizeof line, stdin)) {
        if (!strchr(line, '\n')) {                 /* overlong line: discard the rest */
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) { }
            say(0, "Command too long.");
        } else {
            handle(line);
        }
        respond();
    }

    pl_free(&playlist); liked_free(&liked);
    lstack_free(&histL); lq_free(&queueL);
    return 0;
}
