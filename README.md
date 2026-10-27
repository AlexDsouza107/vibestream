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

In the UI: an obsidian, frosted-glass layout with minimal outline icons, a copper accent taken from the VibeStream logo, a mesh
gradient behind the playlist header, and a light/dark switch (sun/moon, top right).

**Playlists and shuffle**
- **Create playlists** with the `+` next to *Library*. Add songs from the playlist's *Add songs* button, from the `•••` menu on any
  song (*Add to playlist*), or by uploading files while the playlist is open. Rename or delete from the playlist page.
  Each playlist is its own **doubly linked list**; the main *VibeStream Mix* is the library of all songs.
- **Shuffle** is on the Play row of every list (Mix, Liked Songs, your playlists) and in the player bar. When it is on, the list
  you are playing is Fisher-Yates shuffled into a **linked-list queue** and consumed one song at a time, so every song plays once
  before any repeats. With Repeat All on, it reshuffles when the queue runs out.
- Play always continues *inside the list you started from* (Mix, Liked Songs or a playlist), after anything in Up Next.

**Up Next (the queue)** - open it with the queue button. It shows everything queued as one list:
- **Reorder** by dragging a song (a line shows where it will land) or with the keyboard: focus the grip, press Arrow Up/Down.
- **Remove** a song with its `×`, **Clear** the lot, or **Add songs** from a picker. *Play next* songs sit at the top.
- Under the hood the queue is the priority queue (Play next) + deque + circular/linked queue. Removing a song edits just that
  structure; reordering rebuilds them (Play next entries keep their label only while they stay at the top). In array mode each
  queue holds at most 10 songs; switch to linked-list mode (sidebar) for more.
- Below the queue, *Next from ...* previews what the current list will play.

**Playing your own music:** press **Add song** (or drag audio files onto the song list) to add MP3 / M4A / WAV / OGG / FLAC files.
- Each file becomes a normal library entry, so queue, Play next, like, playlists etc. all work.
- Title and artist come from the file's ID3 tags (`web/meta.js` reads them, plus embedded cover art); otherwise from the
  file name (`Artist - Title.mp3`, else artist = "Local Track"). Duration is read from the file itself.
- Playback uses one HTML5 `<audio>` element: play/pause, seek (click, drag or arrow keys on the bar), volume, previous/next,
  and auto-advance. Songs without a file (the 8 samples, or "Insert manually") play on a silent simulated clock.
- Files stay in your browser (`blob:` URLs, nothing is uploaded). Removing a track revokes its URL. Because the files are not
  saved, reloading the page removes the uploaded tracks from the library.

Other controls: click the player artwork for full-screen Now Playing; double-click a song to play it; Space = play/pause.
"Insert manually" adds a song at a chosen position of the library list.

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
