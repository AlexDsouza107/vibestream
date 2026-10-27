// app.js - VibeStream front end.
//   * The C engine owns the data structures (playlist, queues, stacks...). We send it commands and draw the state it returns.
//   * Local audio files live in the browser only: a `media` map links a song id to its blob: URL and cover art.
//   * One HTML5 <audio> element plays uploaded files; songs without a file use a simulated clock (no sound).
const $ = (id) => document.getElementById(id);

/* ================================================================== */
/* 1. State                                                            */
/* ================================================================== */
let st = null;             // latest state from the engine
let view = 'home';         // 'home' | 'liked' | 'pl:<id>' (a playlist you created)
let filter = '';
let elapsed = 0;           // seconds into the current song
let playing = false;
let animateRows = true;    // rows slide in only when the view changes, not on every update
let scrubbing = false;     // true while the user drags a progress bar

const media = {};          // song id -> { url, art, name }   (blob: URLs for uploaded files)
const audio = new Audio(); // the single audio engine
audio.preload = 'auto';

/* ================================================================== */
/* 2. Icons - minimal 1.5px outline set                                */
/* ================================================================== */
const I = (inner, fill) => '<svg viewBox="0 0 24 24" fill="' + (fill || 'none') + '" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + inner + '</svg>';
const HEART = '<path d="M19 14c1.5-1.5 3-3.2 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.8 0-3 .5-4.5 2C10.5 3.5 9.3 3 7.5 3A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.1 3 5.5l7 7z"/>';
const ICONS = {
  logo: I('<path d="M2.5 13q2.2-8 4.5 0t4.5 0 4.5-10 4.5 10 1.5 0"/><path d="M3 20h18" opacity=".5"/>'),
  search: I('<circle cx="11" cy="11" r="7"/><path d="m21 21-4.3-4.3"/>'),
  compass: I('<circle cx="12" cy="12" r="9.5"/><path d="m16 8-2 6-6 2 2-6z"/>'),
  heart: I(HEART), 'heart-fill': I(HEART, 'currentColor'),
  list: I('<path d="M21 15V6"/><circle cx="18.5" cy="15.5" r="2.5"/><path d="M12 12H3M16 6H3M12 18H3"/>'),
  play: I('<path d="M6 4l14 8-14 8z"/>'),
  pause: I('<rect x="6" y="4" width="4" height="16" rx="1"/><rect x="14" y="4" width="4" height="16" rx="1"/>'),
  prev: I('<path d="M19 20 9 12l10-8z"/><path d="M5 19V5"/>'),
  next: I('<path d="m5 4 10 8-10 8z"/><path d="M19 5v14"/>'),
  repeat: I('<path d="m17 2 4 4-4 4"/><path d="M3 11V10a4 4 0 0 1 4-4h14"/><path d="m7 22-4-4 4-4"/><path d="M21 13v1a4 4 0 0 1-4 4H3"/>'),
  vol: I('<path d="M11 5 6 9H2v6h4l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M19 5a10 10 0 0 1 0 14"/>'),
  mute: I('<path d="M11 5 6 9H2v6h4l5 4z"/><path d="m22 9-6 6M16 9l6 6"/>'),
  upload: I('<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m17 8-5-5-5 5"/><path d="M12 3v12"/>'),
  close: I('<path d="M18 6 6 18M6 6l12 12"/>'),
  chev: I('<path d="m6 9 6 6 6-6"/>'),
  shuffle: I('<path d="M2 18h1.4c1.3 0 2.5-.6 3.3-1.7l6.1-8.6c.7-1.1 2-1.7 3.3-1.7H22"/><path d="m18 2 4 4-4 4"/><path d="M2 6h1.9c1.5 0 2.9.9 3.6 2.2"/><path d="M22 18h-5.9c-1.3 0-2.6-.7-3.3-1.8l-.5-.8"/><path d="m18 14 4 4-4 4"/>'),
  plus: I('<path d="M12 5v14M5 12h14"/>'),
  grip: I('<circle cx="9" cy="6" r="1"/><circle cx="15" cy="6" r="1"/><circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><circle cx="9" cy="18" r="1"/><circle cx="15" cy="18" r="1"/>'),
  disc: I('<circle cx="12" cy="12" r="9.5"/><circle cx="12" cy="12" r="3"/><path d="M12 6.2a5.8 5.8 0 0 1 5.8 5.8" opacity=".6"/>'),
  sun: '<svg class="i-sun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M18.7 5.3l-1.4 1.4M6.7 17.3l-1.4 1.4"/></svg>',
  moon: '<svg class="i-moon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9z"/></svg>',
};
const ico = (n) => ICONS[n] || '';
document.querySelectorAll('[data-icon]').forEach((e) => { e.innerHTML = ico(e.dataset.icon); });

/* ================================================================== */
/* 3. Small helpers                                                    */
/* ================================================================== */
function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;   // textContent: file names can never inject HTML
  return e;
}
function iconBtn(name, cls, label) {
  const b = el('button', cls);
  b.innerHTML = ico(name);
  if (label) b.setAttribute('aria-label', label);
  return b;
}
function fmt(sec) {
  sec = Math.max(0, Math.floor(sec));
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  return h ? h + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0') : m + ':' + String(s).padStart(2, '0');
}
function fmtTotal(sec) {
  const m = Math.max(1, Math.round(sec / 60));
  return m >= 60 ? Math.floor(m / 60) + ' hr ' + (m % 60) + ' min' : m + ' min';
}
const HUES = [28, 205, 345, 160, 262, 42, 318, 218];            // muted tonal colours for artwork and the mesh
const hue = (id) => HUES[(id - 1 + HUES.length * 100) % HUES.length];
const current = () => (st && st.history.length ? st.history[0] : null);   // top of the history stack = now playing
const isLiked = (id) => st.liked.some((s) => s.id === id);
const backed = () => { const c = current(); return !!(c && media[c.id]); };  // does the current song have a real audio file?

let toastTimer;
function toast(msg, ok) {
  const t = $('toast');
  t.textContent = msg;
  t.className = 'toast glass show' + (ok ? '' : ' bad');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = 'toast glass' + (ok ? '' : ' bad'); }, 2600);
}

/* ================================================================== */
/* 4. Media library: blob URLs for uploaded files                      */
/* ================================================================== */
const LOCAL_KEY = 'vs-local-ids';
function savedLocalIds() { try { return JSON.parse(localStorage.getItem(LOCAL_KEY) || '[]'); } catch (e) { return []; } }
function saveLocalIds(ids) { try { localStorage.setItem(LOCAL_KEY, JSON.stringify(ids)); } catch (e) { /* storage blocked */ } }

function registerMedia(id, info) {
  media[id] = info;
  saveLocalIds([...new Set(savedLocalIds().concat(id))]);
}

// Free the blob: URLs of a removed track so the browser can release the file's memory.
function releaseMedia(id) {
  const m = media[id];
  if (!m) return;
  if (audio.src === m.url) {
    audio.pause();
    audio.removeAttribute('src');
    audio.load();
    const c = current();
    if (c && c.id === Number(id)) playing = false;
  }
  URL.revokeObjectURL(m.url);
  if (m.art) URL.revokeObjectURL(m.art);
  delete media[id];
  saveLocalIds(savedLocalIds().filter((x) => String(x) !== String(id)));
}

// Artwork: embedded cover if the file has one, a vinyl disc for other uploads, a tonal initial for sample songs.
function decorate(c, song) {
  const m = media[song.id];
  c.replaceChildren();
  c.classList.remove('has-art', 'vinyl');
  c.style.backgroundImage = '';
  c.style.setProperty('--h', hue(song.id));
  if (m && m.art) { c.classList.add('has-art'); c.style.backgroundImage = 'url("' + m.art + '")'; }
  else if (m) { c.classList.add('vinyl'); c.innerHTML = ico('disc'); }
  else c.append(el('span', '', (song.title || '?')[0].toUpperCase()));
}
function cover(song, cls) {
  const c = el('div', 'cover ' + (cls || ''));
  decorate(c, song);
  return c;
}

/* ================================================================== */
/* 5. Talking to the C engine                                          */
/* ================================================================== */
async function cmd(text, opts) {
  try {
    const r = await fetch('/api/cmd', { method: 'POST', body: JSON.stringify({ cmd: text }) });
    const d = await r.json();
    const started = d.ok && d.msg.startsWith('Now playing');
    st = d.state;
    if (started) startPlayback();
    else if (!d.ok && /End of playlist|Nothing to play/.test(d.msg)) setPlaying(false);
    if (!started && !(opts && opts.quiet && d.ok)) toast(d.msg, d.ok);
    render();
    return d;
  } catch (e) {
    toast('Cannot reach the server. Is it running?', false);
  }
}

/* ================================================================== */
/* 6. Audio engine                                                     */
/* ================================================================== */
let switching = false;

// Called whenever the engine says a new song is now playing.
function startPlayback() {
  const cur = current();
  elapsed = 0;
  playing = true;
  if (cur && media[cur.id]) {
    switching = true;
    audio.src = media[cur.id].url;
    audio.currentTime = 0;
    audio.play().catch(onPlayError).finally(() => { switching = false; });
  } else {
    audio.pause();            // simulated song: no sound, the clock below drives the bar
  }
}

function onPlayError() {
  playing = false;
  toast('This file could not be played by your browser.', false);
  render();
}

function setPlaying(p) {
  playing = p;
  if (backed()) { if (p) audio.play().catch(onPlayError); else audio.pause(); }
}

function togglePlay() {
  if (!current()) { cmd('play'); return; }
  setPlaying(!playing);
  render();
}

audio.addEventListener('timeupdate', () => { if (!scrubbing && backed()) { elapsed = audio.currentTime; paintTime(); } });
audio.addEventListener('ended', () => cmd('play'));            // auto-advance through the queue / playlist
audio.addEventListener('play', () => { if (backed() && !playing) { playing = true; render(); } });
audio.addEventListener('pause', () => { if (!audio.ended && !switching && backed() && playing) { playing = false; render(); } });
audio.addEventListener('error', () => { if (audio.getAttribute('src')) onPlayError(); });

// Simulated clock for songs that have no audio file (the 8 sample songs and manually added ones).
setInterval(() => {
  const cur = current();
  if (!playing || !cur || backed() || scrubbing) return;
  elapsed++;
  if (elapsed >= cur.duration) { cmd('play'); return; }
  paintTime();
}, 1000);

function seekTo(t) {
  const cur = current();
  if (!cur) return;
  const dur = cur.duration;
  elapsed = Math.max(0, Math.min(dur, t));
  if (backed()) audio.currentTime = Math.min(elapsed, isFinite(audio.duration) ? audio.duration : elapsed);
  paintTime();
}

/* volume */
let volume = 80, muted = false;
try { const v = localStorage.getItem('vs-vol'); if (v !== null) volume = Math.min(100, Math.max(0, Number(v))); } catch (e) { /* ignore */ }
function applyVolume() {
  audio.volume = muted ? 0 : volume / 100;
  const v = muted ? 0 : volume;
  $('vol').value = v;
  $('vol').style.setProperty('--v', v + '%');
  $('b-mute').innerHTML = ico(v === 0 ? 'mute' : 'vol');
  $('b-mute').setAttribute('aria-label', v === 0 ? 'Unmute' : 'Mute');
}
$('vol').oninput = (e) => {
  volume = Number(e.target.value); muted = volume === 0;
  try { localStorage.setItem('vs-vol', String(volume)); } catch (err) { /* ignore */ }
  applyVolume();
};
$('b-mute').onclick = () => { muted = !muted; if (!muted && volume === 0) volume = 60; applyVolume(); };
applyVolume();

/* progress bars (player + Now Playing): click, drag and keyboard accessible */
function paintTime() {
  const cur = current();
  const dur = cur ? cur.duration : 0;
  const e = Math.min(elapsed, dur);
  const pct = dur ? (e / dur) * 100 + '%' : '0%';
  [['t-cur', 't-total', 'fill', 'bar'], ['np-cur', 'np-total', 'np-fill', 'np-bar']].forEach(([c, t, f, b]) => {
    $(c).textContent = fmt(e);
    $(t).textContent = '-' + fmt(dur - e);
    $(f).style.width = pct;
    const bar = $(b);
    bar.setAttribute('aria-valuemax', String(Math.floor(dur)));
    bar.setAttribute('aria-valuenow', String(Math.floor(e)));
    bar.setAttribute('aria-valuetext', fmt(e) + ' of ' + fmt(dur));
  });
}
function bindScrub(bar) {
  const at = (ev) => { const r = bar.getBoundingClientRect(); const cur = current(); return cur ? Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width)) * cur.duration : 0; };
  bar.addEventListener('pointerdown', (ev) => {
    if (!current()) return;
    scrubbing = true; bar.classList.add('drag'); bar.setPointerCapture(ev.pointerId);
    seekTo(at(ev));
  });
  bar.addEventListener('pointermove', (ev) => { if (scrubbing && bar.hasPointerCapture(ev.pointerId)) seekTo(at(ev)); });
  const end = () => { scrubbing = false; bar.classList.remove('drag'); };
  bar.addEventListener('pointerup', end);
  bar.addEventListener('pointercancel', end);
  bar.addEventListener('keydown', (ev) => {
    const cur = current();
    if (!cur) return;
    const step = { ArrowLeft: -5, ArrowDown: -5, ArrowRight: 5, ArrowUp: 5, PageDown: -30, PageUp: 30 }[ev.key];
    if (step !== undefined) { ev.preventDefault(); seekTo(elapsed + step); }
    else if (ev.key === 'Home') { ev.preventDefault(); seekTo(0); }
    else if (ev.key === 'End') { ev.preventDefault(); seekTo(cur.duration); }
  });
}
bindScrub($('bar'));
bindScrub($('np-bar'));

const goPrev = () => {
  if (elapsed > 3 || !st || st.history.length < 2) seekTo(0);     // like most players: restart first
  else cmd('prev');                                               // otherwise pop the history stack
};
['b-play', 'np-play'].forEach((id) => { $(id).onclick = togglePlay; });
['b-next', 'np-next'].forEach((id) => { $(id).onclick = () => cmd('play'); });
['b-prev', 'np-prev'].forEach((id) => { $(id).onclick = goPrev; });
['b-repeat', 'np-repeat'].forEach((id) => { $(id).onclick = () => cmd('repeat'); });
['b-shuffle', 'np-shuffle'].forEach((id) => { $(id).onclick = () => cmd('shuffle'); });
$('btn-mode').onclick = () => cmd('mode');

/* ================================================================== */
/* 7. Rendering                                                        */
/* ================================================================== */
/* ---------- which list is on screen ---------- */
const customId = () => (view.startsWith('pl:') ? Number(view.slice(3)) : 0);
const viewCtx = () => (view === 'home' ? 0 : view === 'liked' ? 1 : customId());
const customList = (id) => (st.playlists || []).find((l) => l.id === id);
function viewList() {
  if (view === 'home') return st.playlist;
  if (view === 'liked') return st.liked;
  const l = customList(customId());
  return l ? l.songs : [];
}
function ctxName(c) {
  return c === 0 ? 'VibeStream Mix' : c === 1 ? 'Liked Songs' : (customList(c) || { name: 'your playlist' }).name;
}

function render() {
  if (!st) return;
  if (view.startsWith('pl:') && !customList(customId())) view = 'home';   // that playlist was deleted
  renderLibrary();
  renderView();
  renderPlayer();
  if ($('app').classList.contains('q-open')) renderQueue();
  ['b-repeat', 'np-repeat'].forEach((id) => {
    $(id).classList.toggle('on', !!st.repeat);
    $(id).setAttribute('aria-pressed', String(!!st.repeat));
  });
  ['b-shuffle', 'np-shuffle'].forEach((id) => {
    $(id).classList.toggle('on', !!st.shuffle);
    $(id).setAttribute('aria-pressed', String(!!st.shuffle));
  });
  $('btn-mode').textContent = st.dynamic ? 'Linked list mode' : 'Array mode';
  document.querySelectorAll('.nav-item[data-view]').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
}

/* sidebar library: the main mix, liked songs and every playlist you created */
function renderLibrary() {
  const lib = $('lib');
  lib.replaceChildren();
  const entries = [{ v: 'home', ctx: 0, name: 'VibeStream Mix', n: st.playlist.length, icon: 'disc' },
    { v: 'liked', ctx: 1, name: 'Liked Songs', n: st.liked.length, icon: 'heart' }]
    .concat((st.playlists || []).map((l) => ({ v: 'pl:' + l.id, ctx: l.id, name: l.name, n: l.songs.length, icon: 'list', first: l.songs[0] })));
  entries.forEach((e) => {
    const b = el('button', 'lib-item' + (view === e.v ? ' active' : ''));
    const c = el('div', 'cover sm');
    if (e.first && media[e.first.id]) decorate(c, e.first); else c.innerHTML = ico(e.icon);
    const t = el('div');
    t.append(el('div', 'lt', e.name), el('div', 'ls', 'Playlist · ' + e.n + ' song' + (e.n === 1 ? '' : 's') + (st.ctx === e.ctx && playing ? ' · playing' : '')));
    b.append(c, t);
    b.onclick = () => setView(e.v);
    lib.appendChild(b);
  });
  // phone layout: the same lists as chips
  const m = $('mnav');
  m.replaceChildren();
  entries.forEach((e) => {
    const b = el('button', 'chip-btn' + (view === e.v ? ' active' : ''), e.name);
    b.onclick = () => setView(e.v);
    m.appendChild(b);
  });
  const nb = el('button', 'chip-btn', '+ New');
  nb.onclick = () => createPlaylist();
  m.appendChild(nb);
}

function renderView() {
  const root = $('view');
  const main = $('main');
  const scroll = main.scrollTop;
  root.replaceChildren();
  const list = viewList();
  const cid = viewCtx();
  const cur = current();
  const anim = animateRows;
  animateRows = false;

  /* header */
  const hero = el('div', 'hero');
  const big = el('div', 'cover lg hero-art');
  const first = list[0];
  if (view === 'liked') { big.style.setProperty('--h', 345); big.innerHTML = ico('heart'); }
  else if (first && media[first.id] && view !== 'home') decorate(big, first);
  else { big.style.setProperty('--h', hue(view === 'home' ? (cur ? cur.id : 1) : cid)); big.innerHTML = ico(view === 'home' ? 'disc' : 'list'); }
  const txt = el('div', 'hero-text');
  txt.append(el('div', 'kind', 'PLAYLIST'), el('h1', '', ctxName(cid)));
  const total = list.reduce((a, x) => a + x.duration, 0);
  const locals = list.filter((x) => media[x.id]).length;
  txt.append(el('div', 'meta', list.length + ' song' + (list.length === 1 ? '' : 's') + (list.length ? ' · ' + fmtTotal(total) : '') + (locals ? ' · ' + locals + ' uploaded' : '')));
  hero.append(big, txt);
  root.appendChild(hero);
  $('stick-title').textContent = ctxName(cid);

  /* actions */
  const bar = el('div', 'toolbar');
  const inThisList = !!(cur && list.some((x) => x.id === cur.id) && st.ctx === cid);
  const bp = el('button', 'btn-primary');
  const showPause = playing && inThisList;
  bp.innerHTML = ico(showPause ? 'pause' : 'play') + '<span>' + (showPause ? 'Pause' : 'Play') + '</span>';
  bp.disabled = !list.length;
  bp.onclick = () => { if (inThisList) togglePlay(); else cmd('play_ctx ' + cid); };
  const sh = el('button', 'btn-ghost' + (st.shuffle ? ' on' : ''));
  sh.innerHTML = ico('shuffle') + '<span>Shuffle</span>';
  sh.setAttribute('aria-pressed', String(!!st.shuffle));
  sh.title = st.shuffle ? 'Shuffle is on: lists play in random order' : 'Play this list in random order';
  sh.onclick = () => cmd('shuffle');
  bar.append(bp, sh);
  const upload = el('button', 'btn-ghost');
  upload.innerHTML = ico('upload') + '<span>' + (view === 'home' ? 'Add song' : 'Upload') + '</span>';
  upload.setAttribute('aria-label', 'Upload audio files from your computer');
  upload.onclick = () => $('file-input').click();
  if (view === 'home') {
    const manual = el('button', 'text-btn', 'Insert manually');
    manual.title = 'Add a song by typing its details, optionally at a chosen position';
    manual.onclick = () => $('add-dialog').showModal();
    bar.append(upload, manual);
  } else if (cid >= 2) {
    const add = el('button', 'btn-ghost');
    add.innerHTML = ico('plus') + '<span>Add songs</span>';
    add.onclick = () => openPicker({ title: 'Add to ' + ctxName(cid), songs: st.playlist.filter((x) => !list.some((y) => y.id === x.id)), cta: 'Add',
      run: (ids) => addToList(cid, ids) });
    const ren = el('button', 'text-btn', 'Rename');
    ren.onclick = async () => { const n = await askName('Rename playlist', ctxName(cid), 'Save'); if (n) cmd('list_rename ' + cid + '|' + cleanField(n, 40)); };
    const del = el('button', 'text-btn danger', 'Delete');
    del.onclick = () => { if (confirm('Delete the playlist "' + ctxName(cid) + '"? Your songs stay in the library.')) { setView('home'); cmd('list_delete ' + cid); } };
    bar.append(add, upload, ren, del);
  }
  root.appendChild(bar);

  /* song table */
  const q = filter.trim().toLowerCase();
  const rows = list.map((x, i) => ({ s: x, i })).filter(({ s: x }) => !q || x.title.toLowerCase().includes(q) || x.artist.toLowerCase().includes(q));
  const wrap = el('div', 'list');
  const th = el('div', 'th');
  ['#', 'Song', 'Time', ''].forEach((h) => th.appendChild(el('div', '', h)));
  wrap.appendChild(th);
  if (!rows.length) {
    wrap.appendChild(el('div', 'empty', list.length ? 'No songs match your search.'
      : view === 'liked' ? 'Tap the heart on a song to save it here.'
      : view === 'home' ? 'Drop audio files here, or choose Add song.'
      : 'This playlist is empty. Choose Add songs, or use ••• on any song.'));
  }
  rows.forEach(({ s: x, i }, k) => {
    const isCur = cur && cur.id === x.id;
    const r = el('div', 'tr' + (isCur ? ' cur' : '') + (anim ? ' reveal' : ''));
    r.style.setProperty('--i', Math.min(k, 14));
    const num = el('div', 'num');
    if (isCur) {
      const eq = el('span', 'eq' + (playing ? '' : ' paused'));
      eq.append(el('i'), el('i'), el('i'));
      num.appendChild(eq);
    } else {
      num.appendChild(el('span', 'n', i + 1));
      const pl = iconBtn('play', 'pl', 'Play ' + x.title);
      pl.onclick = (e) => { e.stopPropagation(); cmd('play_id ' + x.id + '|' + cid); };
      num.appendChild(pl);
    }
    const info = el('div', 'info');
    const names = el('div');
    names.append(el('div', 't', x.title), el('div', 'a', x.artist));
    info.append(cover(x, 'xs'), names);
    const more = el('button', 'more', '•••');
    more.setAttribute('aria-label', 'More options for ' + x.title);
    more.onclick = (e) => { e.stopPropagation(); openMenu(more, x); };
    r.append(num, info, el('div', 'dur', fmt(x.duration)), more);
    r.ondblclick = () => cmd('play_id ' + x.id + '|' + cid);
    r.oncontextmenu = (e) => { e.preventDefault(); openMenu(more, x); };
    wrap.appendChild(r);
  });
  root.appendChild(wrap);
  main.scrollTop = scroll;
  watchReveals();
  onScroll();
}

function renderPlayer() {
  const cur = current();
  document.body.style.setProperty('--amb', cur ? hue(cur.id) : 30);   // the mesh gradient follows the song

  const left = $('p-left');
  left.replaceChildren();
  const names = el('div');
  if (cur) {
    names.append(el('div', 't', cur.title), el('div', 'a', cur.artist));
    left.append(cover(cur), names);
  } else {
    const c = el('div', 'cover'); c.innerHTML = ico('disc');
    names.append(el('div', 't', 'Not playing'), el('div', 'a', 'Choose a song'));
    left.append(c, names);
  }
  const pi = ico(playing ? 'pause' : 'play');
  ['b-play', 'np-play'].forEach((id) => { $(id).innerHTML = pi; $(id).setAttribute('aria-label', playing ? 'Pause' : 'Play'); });

  /* Now Playing overlay */
  $('np').classList.toggle('playing', !!(cur && playing));
  const art = $('np-art');
  if (cur) decorate(art, cur); else { art.replaceChildren(); art.innerHTML = ico('disc'); }
  $('np-title').textContent = cur ? cur.title : 'Not playing';
  $('np-artist').textContent = cur ? cur.artist : 'Choose a song';
  const like = $('np-like');
  const liked = !!(cur && isLiked(cur.id));
  like.innerHTML = ico(liked ? 'heart-fill' : 'heart');
  like.className = 'icon-btn' + (liked ? ' on' : '');
  like.setAttribute('aria-label', liked ? 'Unlike' : 'Like');
  like.onclick = () => cur && cmd((liked ? 'unlike ' : 'like ') + cur.id);
  paintTime();
}

let focusQueueIdx = -1;          // keep keyboard focus on a song after it was moved
function renderQueue() {
  const body = $('queue-body');
  body.replaceChildren();
  const cur = current();
  const mini = (song, badge, k) => {
    const d = el('div', 'q-item');
    d.style.animationDelay = (k || 0) * 30 + 'ms';
    const n = el('div', 'qn');
    n.append(el('div', 't', song.title), el('div', 'a', song.artist));
    d.append(cover(song, 'xs'), n);
    if (badge) d.appendChild(el('span', 'q-badge', badge));
    return d;
  };

  body.appendChild(el('div', 'q-sec', 'Now playing'));
  body.appendChild(cur ? mini(cur) : el('div', 'a empty', 'Nothing playing'));

  /* the editable queue */
  const up = st.upnext;
  const head = el('div', 'q-sec-row');
  head.appendChild(el('div', 'q-sec', 'Next in queue' + (up.length ? ' · ' + up.length : '')));
  if (up.length) {
    const clr = el('button', 'text-btn', 'Clear');
    clr.onclick = () => cmd('q_clear');
    head.appendChild(clr);
  }
  body.appendChild(head);
  const list = el('div', 'q-list');
  list.setAttribute('role', 'list');
  up.forEach((e, i) => {
    const d = el('div', 'q-item');
    d.draggable = true;
    d.dataset.idx = i;
    d.setAttribute('role', 'listitem');
    d.style.animationDelay = Math.min(i, 8) * 25 + 'ms';
    const grip = el('button', 'grip');
    grip.innerHTML = ico('grip');
    grip.setAttribute('aria-label', 'Reorder ' + e.song.title + '. Drag, or press arrow up or down.');
    grip.onkeydown = (ev) => {
      if (ev.key !== 'ArrowUp' && ev.key !== 'ArrowDown') return;
      ev.preventDefault();
      const to = i + (ev.key === 'ArrowUp' ? -1 : 1);
      if (to < 0 || to >= up.length) return;
      focusQueueIdx = to;
      cmd('q_move ' + i + '|' + to, { quiet: true });
    };
    const n = el('div', 'qn');
    n.append(el('div', 't', e.song.title), el('div', 'a', e.song.artist));
    const x = iconBtn('close', 'q-x', 'Remove ' + e.song.title + ' from the queue');
    x.onclick = () => cmd('q_remove_at ' + i, { quiet: true });
    d.append(grip, cover(e.song, 'xs'), n);
    if (e.kind === 'next') d.appendChild(el('span', 'q-badge', 'Play next'));
    d.appendChild(x);
    list.appendChild(d);
  });
  if (!up.length) body.appendChild(el('div', 'q-note', 'Nothing queued. Add songs below, or use ••• on any song.'));
  else bindQueueDnD(list);
  body.appendChild(list);
  const add = el('button', 'btn-ghost q-add');
  add.innerHTML = ico('plus') + '<span>Add songs</span>';
  add.onclick = () => openPicker({ title: 'Add to Up Next', songs: st.playlist, cta: 'Add to queue', run: (ids) => addToQueue(ids) });
  body.appendChild(add);

  /* what plays after the queue (read-only) */
  if (st.upcoming.length) {
    body.appendChild(el('div', 'q-sec', 'Next from ' + ctxName(st.ctx) + (st.shuffle ? ' · shuffled' : '')));
    st.upcoming.forEach((x, k) => body.appendChild(mini(x, '', k)));
  }
  if (focusQueueIdx >= 0) {
    const g = body.querySelector('.q-item[data-idx="' + focusQueueIdx + '"] .grip');
    if (g) g.focus();
    focusQueueIdx = -1;
  }
}

/* drag to reorder: a line shows where the song will land */
function bindQueueDnD(list) {
  let from = -1;
  const clearMarks = () => list.querySelectorAll('.drop-before,.drop-after,.dragging').forEach((n) => n.classList.remove('drop-before', 'drop-after', 'dragging'));
  const slot = (y) => {                                      // index the song would be inserted before
    const items = [...list.querySelectorAll('.q-item')];
    for (let i = 0; i < items.length; i++) { const r = items[i].getBoundingClientRect(); if (y < r.top + r.height / 2) return i; }
    return items.length;
  };
  list.addEventListener('dragstart', (e) => {
    const it = e.target.closest('.q-item');
    if (!it) return;
    from = Number(it.dataset.idx);
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', String(from));
    requestAnimationFrame(() => it.classList.add('dragging'));
  });
  list.addEventListener('dragover', (e) => {
    if (from < 0) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';
    list.querySelectorAll('.drop-before,.drop-after').forEach((n) => n.classList.remove('drop-before', 'drop-after'));
    const items = [...list.querySelectorAll('.q-item')];
    const i = slot(e.clientY);
    if (i < items.length) items[i].classList.add('drop-before'); else items[items.length - 1].classList.add('drop-after');
  });
  list.addEventListener('drop', (e) => {
    if (from < 0) return;
    e.preventDefault();
    let to = slot(e.clientY);
    if (to > from) to -= 1;                                  // the dragged song leaves its old slot first
    const f = from;
    from = -1;
    clearMarks();
    if (to !== f) cmd('q_move ' + f + '|' + to, { quiet: true });
  });
  list.addEventListener('dragend', () => { from = -1; clearMarks(); });
}

/* ---------- context menu ---------- */
function openMenu(anchor, song) {
  const m = $('menu');
  m.replaceChildren();
  const cid = viewCtx();
  const add = (label, fn, cls) => {
    const b = el('button', cls || '', label);
    b.onclick = () => { closeMenu(); fn(); };
    m.appendChild(b);
  };
  add('Play next', () => cmd('playnext ' + song.id));
  add('Add to queue', () => cmd('q_add ' + song.id));
  add(isLiked(song.id) ? 'Remove from Liked Songs' : 'Save to Liked Songs', () => cmd((isLiked(song.id) ? 'unlike ' : 'like ') + song.id));

  m.appendChild(el('div', 'sep'));
  m.appendChild(el('div', 'lbl', 'Add to playlist'));
  (st.playlists || []).filter((l) => l.id !== cid && !l.songs.some((x) => x.id === song.id))
    .forEach((l) => add(l.name, () => cmd('list_add ' + l.id + '|' + song.id)));
  add('New playlist…', async () => {
    const id = await createPlaylist();
    if (id) cmd('list_add ' + id + '|' + song.id);
  });

  m.appendChild(el('div', 'sep'));
  if (cid >= 2) add('Remove from this playlist', () => cmd('list_remove ' + cid + '|' + song.id));
  if (view === 'home') {
    add(media[song.id] ? 'Remove uploaded track' : 'Delete from library', async () => {
      const d = await cmd('pl_delete ' + song.id);
      if (d && d.ok) { releaseMedia(song.id); render(); }          // revoke the blob: URLs
    }, 'danger');
  }
  m.hidden = false;
  const r = anchor.getBoundingClientRect();
  const w = m.offsetWidth, h = m.offsetHeight;
  m.style.left = Math.max(8, Math.min(r.right - w, innerWidth - w - 8)) + 'px';
  m.style.top = Math.max(8, Math.min(r.bottom + 4, innerHeight - h - 8)) + 'px';
}
function closeMenu() { $('menu').hidden = true; }
document.addEventListener('click', (e) => { if (!e.target.closest('#menu')) closeMenu(); });

/* ================================================================== */
/* 7b. Playlists you create, song picker                               */
/* ================================================================== */
function askName(title, value, cta) {
  return new Promise((resolve) => {
    const d = $('name-dialog'), input = $('name-input');
    $('name-title').textContent = title;
    $('name-ok').textContent = cta;
    input.value = value || '';
    d.returnValue = '';
    d.onclose = () => resolve(d.returnValue === 'ok' ? input.value.trim() : null);
    d.showModal();
    input.select();
  });
}
$('name-cancel').onclick = () => $('name-dialog').close('cancel');

// Ask for a name, create the playlist, open it. Resolves to the new playlist id (or 0).
async function createPlaylist() {
  const name = await askName('New playlist', '', 'Create');
  if (!name) return 0;
  const d = await cmd('list_new ' + cleanField(name, 40));
  const id = d && d.ok && Number((d.msg.match(/\(id (\d+)\)/) || [])[1]);
  if (id) setView('pl:' + id);
  return id || 0;
}
$('new-list').onclick = () => createPlaylist();

let pickRun = null;
function openPicker({ title, songs, cta, run }) {
  const list = $('pick-list');
  list.replaceChildren();
  $('pick-title').textContent = title;
  pickRun = run;
  if (!songs.length) list.appendChild(el('div', 'empty', 'Every song is already here.'));
  songs.forEach((x) => {
    const row = el('label', 'pick-row');
    const cb = el('input');
    cb.type = 'checkbox'; cb.value = x.id;
    const n = el('div');
    n.append(el('div', 't', x.title), el('div', 'a', x.artist));
    row.append(cb, cover(x, 'xs'), n);
    list.appendChild(row);
  });
  const sync = () => {
    const k = list.querySelectorAll('input:checked').length;
    $('pick-ok').textContent = k ? cta + ' (' + k + ')' : cta;
    $('pick-ok').disabled = !k;
  };
  list.onchange = sync;
  sync();
  $('pick-dialog').showModal();
}
$('pick-cancel').onclick = () => $('pick-dialog').close();
$('pick-ok').onclick = async () => {
  const ids = [...$('pick-list').querySelectorAll('input:checked')].map((i) => Number(i.value));
  $('pick-dialog').close();
  if (ids.length && pickRun) await pickRun(ids);
};
async function addToList(cid, ids) {
  let n = 0;
  for (const id of ids) { const d = await cmd('list_add ' + cid + '|' + id, { quiet: true }); if (d && d.ok) n++; }
  toast('Added ' + n + ' song' + (n === 1 ? '' : 's') + ' to ' + ctxName(cid), n > 0);
}
async function addToQueue(ids) {
  let n = 0;
  for (const id of ids) { const d = await cmd('q_add ' + id, { quiet: true }); if (d && d.ok) n++; else break; }
  if (n) toast('Added ' + n + ' song' + (n === 1 ? '' : 's') + ' to Up Next', true);
}

/* ================================================================== */
/* 8. Uploading local audio                                            */
/* ================================================================== */
const cleanField = (s, max) => String(s).replace(/[|\r\n\t]/g, ' ').replace(/\s+/g, ' ').trim().slice(0, max);

async function addFiles(fileList, alsoToList) {
  const files = [...fileList].filter(VSMeta.isAudioFile);
  if (!files.length) { toast('No audio files found. Try MP3, M4A, WAV, OGG or FLAC.', false); return; }
  let added = 0, failed = 0;
  for (const file of files) {
    const url = URL.createObjectURL(file);                  // temporary URL for the audio engine
    let art = null;
    try {
      const [duration, tags] = await Promise.all([VSMeta.readDuration(url), VSMeta.readID3(file)]);
      const name = VSMeta.parseFileName(file.name);
      const title = cleanField(tags.title || name.title, 48) || 'Untitled';
      const artist = cleanField(tags.artist || name.artist, 48) || 'Local Track';
      if (tags.art) art = URL.createObjectURL(new Blob([tags.art.data], { type: tags.art.mime }));
      const d = await cmd('pl_add ' + [title, artist, Math.max(1, Math.round(duration))].join('|'), { quiet: true });
      const id = d && d.ok && (d.msg.match(/\(id (\d+)\)/) || [])[1];
      if (!id) throw new Error('engine refused the song');
      registerMedia(Number(id), { url, art, name: file.name });
      if (alsoToList) await cmd('list_add ' + alsoToList + '|' + id, { quiet: true });   // uploading from a playlist adds it there too
      added++;
      render();
    } catch (e) {
      URL.revokeObjectURL(url);
      if (art) URL.revokeObjectURL(art);
      failed++;
    }
  }
  const parts = [];
  if (added) parts.push('Added ' + added + ' song' + (added === 1 ? '' : 's'));
  if (failed) parts.push(failed + ' file' + (failed === 1 ? '' : 's') + ' could not be read');
  toast(parts.join(' · '), added > 0);
}

$('file-input').addEventListener('change', (e) => { addFiles(e.target.files, customId()); e.target.value = ''; });

/* drag and drop onto the song area */
let dragDepth = 0;
const hasFiles = (e) => e.dataTransfer && [...e.dataTransfer.types].includes('Files');
document.addEventListener('dragover', (e) => { if (hasFiles(e)) e.preventDefault(); });
$('main').addEventListener('dragenter', (e) => { if (hasFiles(e)) { dragDepth++; $('main').classList.add('dragging'); } });
$('main').addEventListener('dragleave', () => { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) $('main').classList.remove('dragging'); });
document.addEventListener('drop', (e) => {
  if (!hasFiles(e)) return;
  e.preventDefault();                                       // never let the browser navigate to the dropped file
  dragDepth = 0;
  $('main').classList.remove('dragging');
  if (e.target.closest && e.target.closest('#main')) addFiles(e.dataTransfer.files, customId());
});

/* uploaded files do not survive a page reload, so tidy up songs whose files are gone */
async function dropOrphans() {
  const ids = savedLocalIds().filter((id) => st.playlist.some((s) => s.id === id) && !media[id]);
  saveLocalIds([]);
  for (const id of ids) await cmd('pl_delete ' + id, { quiet: true });
  if (ids.length) toast('Removed ' + ids.length + ' uploaded track' + (ids.length === 1 ? '' : 's') + ' from your last visit (files are not kept after a reload).', true);
}

/* ================================================================== */
/* 9. Scroll animation, theme, glass highlight, navigation, dialogs    */
/* ================================================================== */
// --sp (0..1) drives the header shrink/fade and the title in the floating pill.
let ticking = false;
function onScroll() {
  if (ticking) return;
  ticking = true;
  requestAnimationFrame(() => {
    $('main').style.setProperty('--sp', Math.min(1, $('main').scrollTop / 240).toFixed(3));
    ticking = false;
  });
}
$('main').addEventListener('scroll', () => { onScroll(); closeMenu(); }, { passive: true });

// rows slide in the first time they enter the viewport
const io = new IntersectionObserver((entries) => {
  entries.forEach((e) => { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } });
}, { root: $('main'), threshold: 0.08 });
function watchReveals() {
  document.querySelectorAll('.tr.reveal').forEach((r) => io.observe(r));
  setTimeout(() => {                                        // safety net: anything on screen must never stay hidden
    const box = $('main').getBoundingClientRect();
    document.querySelectorAll('.tr.reveal:not(.in)').forEach((r) => {
      const b = r.getBoundingClientRect();
      if (b.bottom > box.top && b.top < box.bottom) r.classList.add('in');
    });
  }, 900);
}

/* light / dark theme with a circular reveal */
const htmlEl = document.documentElement;
function paintTheme(theme) {
  htmlEl.dataset.theme = theme;
  document.querySelector('meta[name=theme-color]').content = theme === 'dark' ? '#0A0B10' : '#f3f4f9';
  try { localStorage.setItem('vs-theme', theme); } catch (e) { /* ignore */ }
}
$('theme-btn').innerHTML = ico('sun') + ico('moon');
$('theme-btn').onclick = (e) => {
  const next = htmlEl.dataset.theme === 'dark' ? 'light' : 'dark';
  const r = e.currentTarget.getBoundingClientRect();
  const x = r.left + r.width / 2, y = r.top + r.height / 2;
  if (document.startViewTransition && !matchMedia('(prefers-reduced-motion: reduce)').matches) {
    const vt = document.startViewTransition(() => paintTheme(next));
    vt.ready.then(() => {
      const end = Math.hypot(Math.max(x, innerWidth - x), Math.max(y, innerHeight - y));
      htmlEl.animate({ clipPath: ['circle(0px at ' + x + 'px ' + y + 'px)', 'circle(' + end + 'px at ' + x + 'px ' + y + 'px)'] },
        { duration: 800, easing: 'cubic-bezier(.22,1,.36,1)', pseudoElement: '::view-transition-new(root)' });
    });
  } else {
    htmlEl.classList.add('theming');
    paintTheme(next);
    setTimeout(() => htmlEl.classList.remove('theming'), 600);
  }
};

/* the frosted-glass highlight follows the pointer */
document.addEventListener('pointermove', (e) => {
  const g = e.target.closest && e.target.closest('.glass');
  if (!g) return;
  const r = g.getBoundingClientRect();
  g.style.setProperty('--mx', e.clientX - r.left + 'px');
  g.style.setProperty('--my', e.clientY - r.top + 'px');
}, { passive: true });

/* navigation, queue drawer, Now Playing */
function setView(v) { view = v; animateRows = true; render(); $('main').scrollTop = 0; }
document.querySelectorAll('[data-view]').forEach((b) => { b.onclick = () => setView(b.dataset.view); });

function toggleQueue(open) {
  const app = $('app');
  const want = open === undefined ? !app.classList.contains('q-open') : open;
  app.classList.toggle('q-open', want);
  $('b-queue').classList.toggle('on', want);
  if (want) renderQueue();
}
['open-queue', 'b-queue'].forEach((id) => { $(id).onclick = () => toggleQueue(); });
$('close-queue').onclick = () => toggleQueue(false);

function toggleNP(open) {
  $('np').classList.toggle('open', open);
  $('np').setAttribute('aria-hidden', String(!open));
}
$('p-left').onclick = () => toggleNP(true);
$('np-close').onclick = () => toggleNP(false);
$('np-queue').onclick = () => { toggleNP(false); toggleQueue(true); };

$('search').oninput = (e) => { filter = e.target.value; if (view !== 'home') setView('home'); else render(); };

/* manual add dialog (keeps the "insert at position" linked-list demo) */
$('add-cancel').onclick = () => $('add-dialog').close();
$('add-form').onsubmit = (e) => {
  e.preventDefault();
  const f = e.target;
  const parts = [cleanField(f.title.value, 48), cleanField(f.artist.value, 48), f.duration.value];
  const pos = f.pos.value.trim();
  $('add-dialog').close();
  cmd(pos ? 'pl_insert ' + [pos, ...parts].join('|') : 'pl_add ' + parts.join('|'));
  f.reset();
};

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') { closeMenu(); toggleNP(false); return; }
  if (e.code !== 'Space' || /INPUT|BUTTON|TEXTAREA/.test(e.target.tagName) || e.target.getAttribute('role') === 'slider' || $('add-dialog').open) return;
  e.preventDefault();
  togglePlay();
});

/* start */
cmd('state').then(() => dropOrphans());
