# VibeStream

A menu-driven console music player in C99, written for a Data Structures mini project.
It demonstrates **Module 2 (Stack and Queue)** and **Module 3 (Linked List)** only.
There is no audio or GUI: "playing" a song prints `Now playing: <title> - <artist>`.

## Build and run

```
make          # builds ./spotify  (gcc -std=c99 -Wall -Wextra, no external libraries)
./spotify
make clean    # removes the binary and object files
```

## Features and data structures

| # | Feature | Data structure | Module | File |
|---|---------|----------------|--------|------|
| 1 | Playlist (insert end / at position, delete, forward and backward display, next, previous) | Doubly linked list | 3 | `playlist.c` |
| 2 | Repeat All toggle (last node links to first, unlinked when turned off) | Circular linked list | 3 | `playlist.c` |
| 3 | Liked songs (add, remove, display) | Singly linked list | 3 | `liked.c` |
| 4 | Listening history / "Previous" button (max 10) | Stack (array) | 2 | `stack.c` |
| 5 | Play queue / "Add to queue" (max 10) | Circular queue (array) | 2 | `queue.c` |
| 6 | "Play Next" (HIGH) vs normal (LOW) priority | Priority queue | 2 | `queue.c` |
| 7 | Add to front or rear of the queue | Deque | 2 | `queue.c` |
| 8 | Unlimited history and play queue (menu option 7 switches modes) | Stack and queue using linked lists | 2 + 3 | `stack.c`, `queue.c` |

## Web app (optional front end)

The same C data structures also power a browser UI. The C program `engine` does all the work
(playlist, stacks, queues); `server.js` (plain Node.js, no `npm install`) connects it to the page in `web/`.

```
./run.sh            # builds the C code and starts the server
# then open http://localhost:3000
```

Requirements: `gcc`, `make`, and Node.js 18+. (`npm start` does the same as `./run.sh`; set `PORT=4000` to change the port.)

```
Browser (web/index.html, style.css, app.js)
   |  POST /api/cmd  {"cmd":"play"}
Node server (server.js)
   |  one text command per line on stdin / one JSON line back
C engine (engine.c) -> playlist.c, liked.c, stack.c, queue.c
```

In the UI: a frosted-glass, Apple Music-style layout (sidebar, song list, floating player). The background glow
recolours itself to match the current song. Click the player's artwork for the full-screen Now Playing view;
double-click a song to play it; the `•••` menu has Play next, Add to queue, Save to Liked Songs and Delete.
"Up Next" opens a slide-in drawer showing what plays next. Space = play/pause, click a progress bar to seek.
Playback is simulated (no audio) - when a song "ends" the app asks the C engine for the next one.
The "Array mode / Linked list mode" button in the sidebar switches the history stack and queue implementation.

## How playback works

- **Play** picks the next song in this order: priority queue (HIGH before LOW), then deque (front first), then the play queue, then the next node of the playlist. The song is pushed onto the history stack.
- **Previous** pops the top of history (the song that is playing now), then peeks at the one below it and plays that. The top of history is always the current song.
- If the array history stack is full (10), the oldest entry is dropped so new songs keep being recorded.
- Option 7 switches the history stack and play queue between array and linked-list versions, carrying the contents across (the array versions keep at most 10).
- Memory: every node is `malloc`'d and everything is freed on exit.

## Sample run

```
===== MAIN MENU  [mode: array | repeat: OFF] =====
Choice: 1
Now playing: Blinding Lights - The Weeknd
Choice: 1
Now playing: Shape of You - Ed Sheeran
Choice: 1
Now playing: Believer - Imagine Dragons
Choice: 2
Now playing: Shape of You - Ed Sheeran
Choice: 5          (Queues)  ->  3 (Play Next), song id 8, then 0
Choice: 1
(from priority queue)
Now playing: Counting Stars - OneRepublic
Choice: 8
=== HISTORY (array stack) ===
  TOP> [8] Counting Stars - OneRepublic (4:17)
       [2] Shape of You - Ed Sheeran (3:54)
       [1] Blinding Lights - The Weeknd (3:20)
  (3/10 used)
```
