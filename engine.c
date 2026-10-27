/* engine.c - the VibeStream data-structure engine, driven by text commands instead of a menu.
 * Reads one command per line on stdin, prints one JSON line (result + full state) on stdout.
 * The web server (server.js) talks to this program; all logic lives in the C modules.
 *
 * Data structures used here:
 *   library + user playlists ... doubly linked lists (playlist.c), circular when Repeat All is on
 *   liked songs ............... singly linked list (liked.c)
 *   history ................... stack, array or linked (stack.c)
 *   Up Next ................... priority queue + deque + circular/linked queue (queue.c), shown as ONE list
 *   shuffle ................... a randomised linked-list queue built from the current list (Fisher-Yates)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "song.h"
#include "playlist.h"
#include "liked.h"
#include "stack.h"
#include "queue.h"

#define MAX_LISTS 12
#define LIST_NAME 41

typedef struct {
    int used;
    char name[LIST_NAME];
    Playlist pl;            /* a user playlist is its own doubly linked list */
} UserList;

static Playlist playlist;               /* the library: every song ("VibeStream Mix") */
static UserList lists[MAX_LISTS];
static LikedList liked;
static ArrayStack histA;
static LinkedStack histL;
static CircularQueue queueA;
static LinkedQueue queueL;
static PriorityQueue pq;
static Deque deque;
static LinkedQueue shufq;               /* shuffle order for the current list */

static int dynamic = 0;
static int repeat_on = 0;
static int shuffle_on = 0;
static int shuf_dirty = 1;
static int ctx = 0;                     /* list we are playing from: 0 library, 1 liked, 2+i user list i */
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

/* ================= contexts: which list is "playing from" ================= */

static Playlist *ctx_list(int c) {
    if (c == 0) return &playlist;
    if (c >= 2 && c - 2 < MAX_LISTS && lists[c - 2].used) return &lists[c - 2].pl;
    return NULL;                        /* liked songs (1) or an invalid id */
}
static int ctx_valid(int c) { return c == 1 || ctx_list(c) != NULL; }
static int ctx_count(int c) {
    if (c == 1) return liked.count;
    Playlist *p = ctx_list(c);
    return p ? p->count : 0;
}

/* Copy the songs of a list, in order, into a malloc'd array. Returns the count; *cursor = index of the cursor or -1. */
static int ctx_songs(int c, Song **out, int *cursor) {
    int n = ctx_count(c), i = 0;
    *cursor = -1;
    *out = n ? malloc(sizeof(Song) * n) : NULL;
    if (!n || !*out) return 0;
    if (c == 1) {
        for (LikedNode *x = liked.head; x; x = x->next, i++) {
            (*out)[i] = x->song;
            if (x == liked.current) *cursor = i;
        }
    } else {
        Playlist *p = ctx_list(c);
        PNode *x = p->head;
        for (; i < n; i++, x = x->next) {
            (*out)[i] = x->song;
            if (x == p->current) *cursor = i;
        }
    }
    return n;
}

static void reset_cursors(void) {
    pl_reset_cursor(&playlist);
    for (int i = 0; i < MAX_LISTS; i++) if (lists[i].used) pl_reset_cursor(&lists[i].pl);
    liked_reset_cursor(&liked);
}

/* ================= history, queue helpers (array vs linked) ================= */

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
static int queue_removeAt(int i, Song *s) { return dynamic ? lq_removeAt(&queueL, i, s) : cq_removeAt(&queueA, i, s); }
static int queue_len(void)    { return dynamic ? lq_size(&queueL) : cq_size(&queueA); }

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

/* ================= "Up Next": priority queue + deque + queue shown as one list ================= */

typedef struct { Song song; int next; } UpItem;     /* next = 1 for "Play next" entries */

static int upnext_len(void) { return pq.count + deque.count + queue_len(); }

static UpItem *flatten(int *n) {
    *n = upnext_len();
    UpItem *a = malloc(sizeof(UpItem) * (*n ? *n : 1));
    int k = 0;
    if (!a) { *n = 0; return NULL; }
    for (int i = 0; i < pq.count; i++) { a[k].song = pq.data[i].song; a[k++].next = pq.data[i].priority == PRIO_HIGH; }
    for (int i = 0; i < deque.count; i++) { a[k].song = deque.data[(deque.front + i) % QUEUE_MAX]; a[k++].next = 0; }
    if (dynamic) for (QNode *x = queueL.front; x; x = x->next) { a[k].song = x->song; a[k++].next = 0; }
    else for (int i = 0; i < queueA.count; i++) { a[k].song = queueA.data[(queueA.front + i) % QUEUE_MAX]; a[k++].next = 0; }
    return a;
}

static void clear_upnext(void) {
    Song s;
    while (pq_dequeue(&pq, &s)) { }
    while (dq_deleteFront(&deque, &s)) { }
    while (queue_remove(&s)) { }
}

/* Remove the i-th entry of the combined list from whichever structure holds it. */
static int upnext_removeAt(int i, Song *out) {
    if (i < 0) return 0;
    if (i < pq.count) return pq_removeAt(&pq, i, out);
    i -= pq.count;
    if (i < deque.count) return dq_removeAt(&deque, i, out);
    return queue_removeAt(i - deque.count, out);
}

/* Reorder: take entry `from` and drop it at index `to`. The whole list is rebuilt:
 * "Play next" entries go back into the priority queue, everything else into the play queue. */
static void upnext_move(int from, int to) {
    int n;
    UpItem *a = flatten(&n);
    if (!a || from < 0 || from >= n || to < 0 || to >= n) { free(a); say(0, "Cannot move that song."); return; }
    if (from == to) { free(a); say(1, "ok"); return; }

    int nexts = 0;
    for (int i = 0; i < n; i++) nexts += a[i].next;
    UpItem moved = a[from];
    for (int i = from; i < n - 1; i++) a[i] = a[i + 1];          /* take it out */
    int prefix = nexts - moved.next;                              /* "Play next" block is a prefix */
    /* a "Play next" entry keeps its label while it stays in that block; others join only if dropped strictly inside it */
    moved.next = moved.next ? (to <= prefix) : (to < prefix);
    for (int i = n - 1; i > to; i--) a[i] = a[i - 1];             /* make room */
    a[to] = moved;

    int total_next = 0;
    for (int i = 0; i < n; i++) total_next += a[i].next;
    if (total_next > QUEUE_MAX || (!dynamic && n - total_next > QUEUE_MAX)) {
        free(a);
        say(0, "Array mode holds at most %d songs per queue. Switch to linked-list mode to reorder more.", QUEUE_MAX);
        return;
    }
    clear_upnext();
    for (int i = 0; i < n; i++) {
        if (a[i].next) pq_enqueue(&pq, a[i].song, PRIO_HIGH);
        else queue_add(a[i].song);
    }
    free(a);
    say(1, "ok");
}

/* ================= shuffle: a randomised linked-list queue ================= */

static int current_id(void) {
    Song s;
    return hist_peek(&s) ? s.id : 0;
}

/* Fisher-Yates shuffle of the current list's songs, then enqueue them in that order. */
static void build_shuffle(int exclude_id) {
    Song *arr;
    int cursor, n = ctx_songs(ctx, &arr, &cursor);
    Song s;
    while (lq_dequeue(&shufq, &s)) { }
    int m = 0;
    for (int i = 0; i < n; i++) if (arr[i].id != exclude_id) arr[m++] = arr[i];
    for (int i = m - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Song t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
    for (int i = 0; i < m; i++) lq_enqueue(&shufq, arr[i]);
    free(arr);
    shuf_dirty = 0;
}

static int shuffle_next(Song *out, int exclude_id) {
    if (shuf_dirty) build_shuffle(exclude_id);
    if (lq_dequeue(&shufq, out)) return 1;
    if (repeat_on && ctx_count(ctx) > 0) {                         /* Repeat All: reshuffle and go again */
        build_shuffle(exclude_id);
        return lq_dequeue(&shufq, out);
    }
    return 0;
}

/* ================= JSON output ================= */

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

static void json_list(const Playlist *p) {
    putchar('[');
    PNode *c = p->head;
    for (int i = 0; i < p->count; i++, c = c->next) {
        if (i) putchar(',');
        json_song(&c->song);
    }
    putchar(']');
}

/* The next few songs the current list will play (read-only preview for the Up Next panel). */
static void json_upcoming(void) {
    putchar('[');
    int shown = 0;
    if (shuffle_on) {
        int cid = current_id();
        if (shuf_dirty) build_shuffle(cid);
        for (QNode *x = shufq.front; x && shown < 8; x = x->next, shown++) {
            if (shown) putchar(',');
            json_song(&x->song);
        }
    } else {
        Song *arr;
        int cursor, n = ctx_songs(ctx, &arr, &cursor);
        int avail = cursor >= 0 ? n - 1 : n;
        for (int k = 1; k <= n && shown < 8 && shown < avail; k++) {
            int j = cursor + k;
            if (cursor < 0) j = k - 1;
            if (j >= n) { if (!repeat_on) break; j %= n; }
            if (shown) putchar(',');
            json_song(&arr[j]);
            shown++;
        }
        free(arr);
    }
    putchar(']');
}

static void print_state(void) {
    printf("\"state\":{\"dynamic\":%d,\"repeat\":%d,\"shuffle\":%d,\"ctx\":%d,\"capacity\":%d,\"playlist\":",
           dynamic, repeat_on, shuffle_on, ctx, QUEUE_MAX);
    json_list(&playlist);

    printf(",\"playlists\":[");
    int first = 1;
    for (int i = 0; i < MAX_LISTS; i++) {
        if (!lists[i].used) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"id\":%d,\"name\":", i + 2);
        json_str(lists[i].name);
        printf(",\"songs\":");
        json_list(&lists[i].pl);
        putchar('}');
    }

    printf("],\"liked\":[");
    for (LikedNode *l = liked.head; l; l = l->next) {
        json_song(&l->song);
        if (l->next) putchar(',');
    }

    printf("],\"history\":[");                  /* top of the stack first */
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

    printf("],\"upnext\":[");
    int n;
    UpItem *a = flatten(&n);
    for (int i = 0; i < n; i++) {
        if (i) putchar(',');
        printf("{\"song\":");
        json_song(&a[i].song);
        printf(",\"kind\":\"%s\"}", a[i].next ? "next" : "queue");
    }
    free(a);

    printf("],\"upcoming\":");
    json_upcoming();
    printf("}");
}

static void respond(void) {
    printf("{\"ok\":%s,\"msg\":", res_ok ? "true" : "false");
    json_str(res_msg);
    printf(",");
    print_state();
    printf("}\n");
    fflush(stdout);
}

/* ================= commands ================= */

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

/* Next song from the list we are playing from (shuffled or in order). */
static const Song *source_next(Song *buf) {
    if (shuffle_on) return shuffle_next(buf, current_id()) ? buf : NULL;
    if (ctx == 1) {
        const Song *s = liked_next(&liked);
        if (!s && repeat_on) { liked_reset_cursor(&liked); s = liked_next(&liked); }
        return s;
    }
    Playlist *p = ctx_list(ctx);
    return p ? pl_next(p) : NULL;               /* circular lists wrap by themselves in Repeat All */
}

/* Play order: priority queue (Play next) -> deque -> play queue -> the current list. */
static void cmd_play(void) {
    Song s, buf;
    if (pq_dequeue(&pq, &s)) play_song(&s, " (from Up Next)");
    else if (dq_deleteFront(&deque, &s)) play_song(&s, " (from Up Next)");
    else if (queue_remove(&s)) play_song(&s, " (from Up Next)");
    else {
        const Song *n = source_next(&buf);
        if (n) play_song(n, "");
        else if (ctx_count(ctx) == 0) say(0, "Nothing to play: this list is empty.");
        else {
            reset_cursors();
            shuf_dirty = 1;
            say(0, "End of list. Press Play to start again, or turn on Repeat All.");
        }
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
    if (!to_int(f[first + 2], 1, 86400, &dur)) { say(0, "Duration must be 1-86400 seconds."); return 0; }
    out->id = nextId;
    snprintf(out->title, sizeof out->title, "%s", f[first]);
    snprintf(out->artist, sizeof out->artist, "%s", f[first + 1]);
    out->duration = dur;
    return 1;
}

/* A song was removed from the library: drop it from every other list too. */
static void forget_song(int id) {
    for (int i = 0; i < MAX_LISTS; i++) if (lists[i].used) pl_delete(&lists[i].pl, id);
    liked_remove(&liked, id);
    shuf_dirty = 1;
}

static void cmd_list(const char *cmd, char **f, int nf) {
    int c, id;
    if (!strcmp(cmd, "list_new")) {
        if (!f[0][0]) { say(0, "Give your playlist a name."); return; }
        for (int i = 0; i < MAX_LISTS; i++) {
            if (lists[i].used) continue;
            lists[i].used = 1;
            snprintf(lists[i].name, LIST_NAME, "%s", f[0]);
            pl_init(&lists[i].pl);
            pl_set_repeat(&lists[i].pl, repeat_on);
            say(1, "Created playlist \"%s\" (id %d).", lists[i].name, i + 2);
            return;
        }
        say(0, "You can have up to %d playlists.", MAX_LISTS);
        return;
    }
    if (!to_int(f[0], 2, MAX_LISTS + 1, &c) || !ctx_list(c)) { say(0, "That playlist does not exist."); return; }
    UserList *u = &lists[c - 2];
    if (!strcmp(cmd, "list_rename")) {
        if (nf < 2 || !f[1][0]) { say(0, "Give your playlist a name."); return; }
        snprintf(u->name, LIST_NAME, "%s", f[1]);
        say(1, "Renamed to \"%s\".", u->name);
    } else if (!strcmp(cmd, "list_delete")) {
        char name[LIST_NAME];
        snprintf(name, sizeof name, "%s", u->name);
        pl_free(&u->pl);
        u->used = 0;
        if (ctx == c) { ctx = 0; shuf_dirty = 1; }
        say(1, "Deleted playlist \"%s\".", name);
    } else {                                    /* list_add / list_remove */
        if (nf < 2 || !to_int(f[1], 1, 1000000, &id)) { say(0, "A valid song id is required."); return; }
        if (!strcmp(cmd, "list_add")) {
            const Song *s = pl_find(&playlist, id);
            if (!s) { say(0, "No song with id %d in the library.", id); return; }
            if (pl_find(&u->pl, id)) { say(0, "\"%s\" is already in \"%s\".", s->title, u->name); return; }
            pl_insert_end(&u->pl, *s);
            if (ctx == c) shuf_dirty = 1;
            say(1, "Added \"%s\" to \"%s\".", s->title, u->name);
        } else {
            if (pl_delete(&u->pl, id)) { if (ctx == c) shuf_dirty = 1; say(1, "Removed from \"%s\".", u->name); }
            else say(0, "That song is not in \"%s\".", u->name);
        }
    }
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
    f[nf++] = args;                             /* split args on '|' */
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
    else if (!strcmp(cmd, "play_id")) {          /* play_id <song>[|<list>] : start that song inside a list */
        int c = 0;
        if (nf > 1 && f[1][0] && (!to_int(f[1], 0, MAX_LISTS + 1, &c) || !ctx_valid(c))) { say(0, "Unknown list."); return; }
        const Song *found = NULL;
        if (c == 1) {
            LikedNode *n = liked.head;
            while (n && n->song.id != id) n = n->next;
            if (n) { liked.current = n; found = &n->song; }
        } else {
            Playlist *p = ctx_list(c);
            PNode *n = p->head;
            for (int i = 0; i < p->count && n; i++, n = n->next)
                if (n->song.id == id) { p->current = n; found = &n->song; break; }
        }
        if (!found) { say(0, "That song is not in this list."); return; }
        ctx = c;
        shuf_dirty = 1;
        play_song(found, "");
    } else if (!strcmp(cmd, "play_ctx")) {       /* play_ctx <list> : play a whole list from its start (random start if shuffled) */
        int c;
        if (!to_int(f[0], 0, MAX_LISTS + 1, &c) || !ctx_valid(c)) { say(0, "Unknown list."); return; }
        if (ctx_count(c) == 0) { say(0, "That list is empty."); return; }
        ctx = c;
        reset_cursors();
        shuf_dirty = 1;
        Song buf;
        if (shuffle_on) { if (shuffle_next(&buf, 0)) play_song(&buf, " (shuffled)"); }
        else { const Song *n = source_next(&buf); if (n) play_song(n, ""); }
    } else if (!strcmp(cmd, "shuffle")) {
        shuffle_on = !shuffle_on;
        shuf_dirty = 1;
        say(1, shuffle_on ? "Shuffle on." : "Shuffle off.");
    } else if (!strcmp(cmd, "pl_add")) {
        if (build_song(f, nf, 0, &s)) {
            if (pl_insert_end(&playlist, s)) { nextId++; shuf_dirty = 1; say(1, "Added \"%s\" at the end (id %d).", s.title, s.id); }
            else say(0, "Could not add song.");
        }
    } else if (!strcmp(cmd, "pl_insert")) {
        if (nf < 4) { say(0, "Position, title, artist and duration are required."); }
        else if (!to_int(f[0], 1, playlist.count + 1, &pos)) say(0, "Position must be 1-%d.", playlist.count + 1);
        else if (build_song(f, nf, 1, &s)) {
            if (pl_insert_at(&playlist, pos, s)) { nextId++; shuf_dirty = 1; say(1, "Inserted \"%s\" at position %d (id %d).", s.title, pos, s.id); }
            else say(0, "Could not insert song.");
        }
    } else if (!strcmp(cmd, "pl_delete")) {
        if (pl_delete(&playlist, id)) { forget_song(id); say(1, "Deleted song %d from the library.", id); }
        else say(0, "No song with id %d.", id);
    } else if (!strncmp(cmd, "list_", 5)) {
        cmd_list(cmd, f, nf);
    } else if (!strcmp(cmd, "like") || !strcmp(cmd, "q_add") || !strcmp(cmd, "playnext") ||
               !strcmp(cmd, "pq_low") || !strcmp(cmd, "dq_front") || !strcmp(cmd, "dq_rear")) {
        const Song *p = pl_find(&playlist, id);
        if (!p) { say(0, "No song with id %d in the library.", id); return; }
        s = *p;
        int ok;
        const char *where;
        if (!strcmp(cmd, "like"))          { ok = liked_add(&liked, s);             where = "Liked Songs"; shuf_dirty = 1; }
        else if (!strcmp(cmd, "q_add"))    { ok = queue_add(s);                     where = "Up Next"; }
        else if (!strcmp(cmd, "playnext")) { ok = pq_enqueue(&pq, s, PRIO_HIGH);    where = "Up Next (play next)"; }
        else if (!strcmp(cmd, "pq_low"))   { ok = pq_enqueue(&pq, s, PRIO_LOW);     where = "the priority queue (LOW)"; }
        else if (!strcmp(cmd, "dq_front")) { ok = dq_insertFront(&deque, s);        where = "the front of the deque"; }
        else                               { ok = dq_insertRear(&deque, s);         where = "the rear of the deque"; }
        if (ok) say(1, "Added \"%s\" to %s.", s.title, where);
        else if (!strcmp(cmd, "like")) say(0, "\"%s\" is already liked.", s.title);
        else say(0, "Cannot add: that queue is full (max %d in array mode).", QUEUE_MAX);
    } else if (!strcmp(cmd, "unlike")) {
        if (liked_remove(&liked, id)) { shuf_dirty = 1; say(1, "Removed from Liked Songs."); } else say(0, "That song is not liked.");
    } else if (!strcmp(cmd, "q_remove")) {
        if (queue_remove(&s)) say(1, "Dequeued \"%s\".", s.title); else say(0, "Play queue is empty.");
    } else if (!strcmp(cmd, "pq_remove")) {
        if (pq_dequeue(&pq, &s)) say(1, "Dequeued \"%s\" from priority queue.", s.title); else say(0, "Priority queue is empty.");
    } else if (!strcmp(cmd, "dq_delfront")) {
        if (dq_deleteFront(&deque, &s)) say(1, "Deleted \"%s\" from deque front.", s.title); else say(0, "Deque is empty.");
    } else if (!strcmp(cmd, "dq_delrear")) {
        if (dq_deleteRear(&deque, &s)) say(1, "Deleted \"%s\" from deque rear.", s.title); else say(0, "Deque is empty.");
    } else if (!strcmp(cmd, "q_remove_at")) {     /* q_remove_at <index in the Up Next list> */
        if (!to_int(f[0], 0, 100000, &pos) || !upnext_removeAt(pos, &s)) say(0, "That song is not in the queue.");
        else say(1, "Removed \"%s\" from Up Next.", s.title);
    } else if (!strcmp(cmd, "q_move")) {          /* q_move <from>|<to> */
        int from, to;
        if (nf < 2 || !to_int(f[0], 0, 100000, &from) || !to_int(f[1], 0, 100000, &to)) say(0, "Invalid move.");
        else upnext_move(from, to);
    } else if (!strcmp(cmd, "q_clear")) {
        clear_upnext();
        say(1, "Up Next cleared.");
    } else if (!strcmp(cmd, "repeat")) {
        repeat_on = !repeat_on;
        pl_set_repeat(&playlist, repeat_on);
        for (int i = 0; i < MAX_LISTS; i++) if (lists[i].used) pl_set_repeat(&lists[i].pl, repeat_on);
        say(1, repeat_on ? "Repeat All on: lists are now circular linked lists." : "Repeat All off: circle unlinked.");
    } else if (!strcmp(cmd, "mode")) {
        toggle_mode();
    } else {
        say(0, "Unknown command.");
    }
}

int main(void) {
    srand((unsigned)time(NULL));
    pl_init(&playlist); liked_init(&liked);
    astack_init(&histA); lstack_init(&histL);
    cq_init(&queueA); lq_init(&queueL);
    pq_init(&pq); dq_init(&deque); lq_init(&shufq);

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
    for (int i = 0; i < MAX_LISTS; i++) if (lists[i].used) pl_free(&lists[i].pl);
    lstack_free(&histL); lq_free(&queueL); lq_free(&shufq);
    return 0;
}
