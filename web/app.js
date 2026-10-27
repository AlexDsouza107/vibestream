// app.js - VibeStream front end. Draws the state sent by the C engine and sends commands back.
const $ = (id) => document.getElementById(id);

let st = null;            // latest state from the engine
let view = 'home';        // 'home' | 'liked'
let filter = '';
let elapsed = 0;          // seconds into the current song (simulated, there is no audio)
let playing = false;
let animateRows = true;   // stagger-in rows only when the view changes, not on every update

/* ---------- icons ---------- */
const P = (d) => '<svg viewBox="0 0 24 24"><path d="' + d + '"/></svg>';
const S = (d) => '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="' + d + '"/></svg>';
const HEART = 'M12 21s-8-5.3-8-11a4.5 4.5 0 0 1 8-2.8A4.5 4.5 0 0 1 20 10c0 5.7-8 11-8 11z';
const ICONS = {
  logo: '<svg viewBox="0 0 32 32"><path d="M3 17q3-9 6 0t6 0 6-11 6 11 3 0" stroke="currentColor" stroke-width="2.4" fill="none" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  home: P('M12 2.7 2 10.4V22h7.5v-6.5h5V22H22V10.4z'),
  heart: P(HEART), 'heart-o': S(HEART),
  search: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><circle cx="10.5" cy="10.5" r="6.5"/><path d="M15.5 15.5 21 21"/></svg>',
  queue: P('M3 5h13v2H3zM3 10h13v2H3zM3 15h8v2H3zM18 10v6.3a2.8 2.8 0 1 0 2 2.7V12h2v-2z'),
  play: P('M7 3.5v17L21 12z'), pause: P('M6 3h4.5v18H6zM13.5 3H18v18h-4.5z'),
  prev: P('M5.5 4h2v16h-2zM20 4.5v15L8.5 12z'), next: P('M16.5 4h2v16h-2zM4 4.5v15L15.5 12z'),
  repeat: S('M17 2l4 4-4 4M3 11V9a3 3 0 0 1 3-3h15M7 22l-4-4 4-4M21 13v2a3 3 0 0 1-3 3H3'),
  close: S('M6 6l12 12M18 6 6 18'),
  chev: S('M6 9l6 6 6-6'),
};
const ico = (n) => ICONS[n] || '';
document.querySelectorAll('[data-icon]').forEach((e) => { e.innerHTML = ico(e.dataset.icon); });

/* ---------- helpers ---------- */
function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;   // textContent: song names can never inject HTML
  return e;
}
function iconBtn(name, cls, label) {
  const b = el('button', cls);
  b.innerHTML = ico(name);
  if (label) b.setAttribute('aria-label', label);
  return b;
}
const mmss = (s) => Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
// each song gets a muted, tonal colour (the CSS keeps saturation low)
const HUES = [28, 205, 345, 160, 262, 42, 318, 218];
const hue = (id) => HUES[(id - 1 + HUES.length * 100) % HUES.length];
function cover(song, cls) {
  const c = el('div', 'cover ' + (cls || ''));
  c.append(el('span', '', (song.title || '?')[0].toUpperCase()));
  c.style.setProperty('--h', hue(song.id));
  return c;
}
const current = () => (st && st.history.length ? st.history[0] : null);
const isLiked = (id) => st.liked.some((s) => s.id === id);

let toastTimer;
function toast(msg, ok) {
  const t = $('toast');
  t.textContent = msg;
  t.className = 'toast show' + (ok ? '' : ' bad');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = 'toast' + (ok ? '' : ' bad'); }, 2300);
}

/* ---------- talking to the C engine ---------- */
async function cmd(text) {
  try {
    const r = await fetch('/api/cmd', { method: 'POST', body: JSON.stringify({ cmd: text }) });
    const d = await r.json();
    const started = d.ok && d.msg.startsWith('Now playing');
    if (started) { elapsed = 0; playing = true; }
    else if (!d.ok && /End of playlist|Nothing to play/.test(d.msg)) playing = false;
    if (!started) toast(d.msg, d.ok);
    st = d.state;
    render();
    return d;
  } catch (e) {
    toast('Cannot reach the server. Is it running?', false);
  }
}

/* ---------- rendering ---------- */
function render() {
  if (!st) return;
  renderView();
  renderPlayer();
  if ($('app').classList.contains('q-open')) renderQueue();
  $('lib-liked-count').textContent = 'Playlist · ' + st.liked.length + ' song' + (st.liked.length === 1 ? '' : 's');
  ['b-repeat', 'np-repeat'].forEach((id) => {
    $(id).classList.toggle('on', !!st.repeat);
    $(id).title = st.repeat ? 'Repeat: on' : 'Repeat: off';
  });
  $('btn-mode').textContent = st.dynamic ? 'Linked list mode' : 'Array mode';
  document.querySelectorAll('.nav-item[data-view]').forEach((b) => b.classList.toggle('active', b.dataset.view === view));
}

function renderView() {
  const root = $('view');
  const main = $('main');
  const scroll = main.scrollTop;
  root.replaceChildren();
  const list = view === 'home' ? st.playlist : st.liked;
  const cur = current();
  const anim = animateRows;
  animateRows = false;

  const hero = el('div', 'hero');
  let big;
  if (view === 'home') { big = cover({ id: 1, title: '♪' }, 'lg'); big.firstChild.textContent = '♪'; }
  else { big = el('div', 'cover lg liked-cover'); const h = el('span'); h.innerHTML = ico('heart'); h.style.fontSize = '80px'; big.append(h); }
  const txt = el('div');
  txt.append(el('div', 'kind', 'PLAYLIST'), el('h1', '', view === 'home' ? 'VibeStream Mix' : 'Liked Songs'));
  const total = list.reduce((a, s) => a + s.duration, 0);
  txt.append(el('div', 'meta', list.length + ' song' + (list.length === 1 ? '' : 's') + ' · ' + Math.round(total / 60) + ' minutes'));
  hero.append(big, txt);
  root.appendChild(hero);

  const bar = el('div', 'toolbar');
  const listPlaying = playing && cur && list.some((s) => s.id === cur.id);
  const bp = el('button', 'btn-primary');
  bp.innerHTML = ico(listPlaying ? 'pause' : 'play') + '<span>' + (listPlaying ? 'Pause' : 'Play') + '</span>';
  bp.onclick = () => {
    if (listPlaying) togglePlay();
    else if (view === 'liked' && list.length) cmd('play_id ' + list[0].id);
    else if (cur && !playing) togglePlay();
    else cmd('play');
  };
  bar.appendChild(bp);
  if (view === 'home') {
    const add = el('button', 'btn-ghost', '+ Add song');
    add.onclick = () => $('add-dialog').showModal();
    bar.appendChild(add);
  }
  root.appendChild(bar);

  const q = filter.trim().toLowerCase();
  const rows = list.map((s, i) => ({ s, i })).filter(({ s }) => !q || s.title.toLowerCase().includes(q) || s.artist.toLowerCase().includes(q));

  const wrap = el('div', 'list');
  const th = el('div', 'th');
  ['#', 'Song', 'Time', ''].forEach((h, i) => th.appendChild(el('div', '', h)));
  th.children[2].style.textAlign = 'right';
  wrap.appendChild(th);

  if (!rows.length) {
    wrap.appendChild(el('div', 'empty', list.length ? 'No songs match your search.' : (view === 'liked' ? 'Tap the heart on a song to save it here.' : 'This playlist is empty. Add a song to get started.')));
  }
  rows.forEach(({ s, i }, k) => {
    const isCur = cur && cur.id === s.id;
    const r = el('div', 'tr' + (isCur ? ' cur' : '') + (anim ? ' anim' : ''));
    r.style.setProperty('--i', Math.min(k, 14));
    const num = el('div', 'num');
    if (isCur) {
      const eq = el('span', 'eq' + (playing ? '' : ' paused'));
      eq.append(el('i'), el('i'), el('i'));
      num.appendChild(eq);
    } else {
      num.appendChild(el('span', 'n', i + 1));
      const pl = iconBtn('play', 'pl', 'Play ' + s.title);
      pl.onclick = (e) => { e.stopPropagation(); cmd('play_id ' + s.id); };
      num.appendChild(pl);
    }
    const info = el('div', 'info');
    const names = el('div');
    names.append(el('div', 't', s.title), el('div', 'a', s.artist));
    info.append(cover(s, 'xs'), names);
    const more = el('button', 'more', '•••');
    more.setAttribute('aria-label', 'More options for ' + s.title);
    more.onclick = (e) => { e.stopPropagation(); openMenu(more, s); };
    r.append(num, info, el('div', 'dur', mmss(s.duration)), more);
    r.ondblclick = () => cmd('play_id ' + s.id);
    r.oncontextmenu = (e) => { e.preventDefault(); openMenu(more, s); };
    wrap.appendChild(r);
  });
  root.appendChild(wrap);
  main.scrollTop = scroll;
}

function renderPlayer() {
  const cur = current();
  // ambient background colour follows the song
  document.body.style.setProperty('--amb', cur ? hue(cur.id) : 30);

  const left = $('p-left');
  left.replaceChildren();
  left.classList.toggle('playing', !!(cur && playing));
  if (cur) {
    const names = el('div');
    names.append(el('div', 't', cur.title), el('div', 'a', cur.artist));
    left.append(cover(cur), names);
  } else {
    const c = el('div', 'cover'); c.style.setProperty('--h', 30); c.append(el('span', '', '♪'));
    const names = el('div');
    names.append(el('div', 't', 'Not playing'), el('div', 'a', 'Choose a song'));
    left.append(c, names);
  }
  const pi = ico(playing ? 'pause' : 'play');
  ['b-play', 'np-play'].forEach((id) => { $(id).innerHTML = pi; $(id).setAttribute('aria-label', playing ? 'Pause' : 'Play'); });

  // Now Playing overlay
  const np = $('np');
  np.classList.toggle('playing', !!(cur && playing));
  const art = $('np-art');
  art.className = 'cover np-art';
  art.replaceChildren(el('span', '', cur ? cur.title[0].toUpperCase() : '♪'));
  art.style.setProperty('--h', cur ? hue(cur.id) : 30);
  $('np-title').textContent = cur ? cur.title : 'Not playing';
  $('np-artist').textContent = cur ? cur.artist : 'Choose a song';
  const like = $('np-like');
  const liked = cur && isLiked(cur.id);
  like.innerHTML = ico(liked ? 'heart' : 'heart-o');
  like.className = 'icon-btn' + (liked ? ' on' : '');
  like.setAttribute('aria-label', liked ? 'Unlike' : 'Like');
  like.onclick = () => cur && cmd((liked ? 'unlike ' : 'like ') + cur.id);
  tick(false);
}

function renderQueue() {
  const body = $('queue-body');
  body.replaceChildren();
  const cur = current();
  const item = (song, badge, k) => {
    const d = el('div', 'q-item');
    d.style.animationDelay = (k || 0) * 30 + 'ms';
    const n = el('div');
    n.append(el('div', 't', song.title), el('div', 'a', song.artist));
    d.append(cover(song, 'xs'), n);
    if (badge) d.appendChild(el('span', 'q-badge', badge));
    return d;
  };
  body.appendChild(el('div', 'q-sec', 'Now playing'));
  body.appendChild(cur ? item(cur) : el('div', 'a empty', 'Nothing playing'));

  const next = [];
  st.priority.forEach((p) => next.push([p.song, p.priority === 'HIGH' ? 'Play next' : '']));
  st.deque.forEach((s) => next.push([s, '']));
  st.queue.forEach((s) => next.push([s, '']));
  body.appendChild(el('div', 'q-sec', 'Up next'));
  if (!next.length) body.appendChild(el('div', 'a empty', 'Your queue is empty. Use ••• on a song to add it.'));
  next.forEach(([s, b], k) => body.appendChild(item(s, b, k)));

  // upcoming playlist songs (what Play does once the queue is empty)
  const pl = st.playlist;
  const idx = pl.findIndex((s) => s.id === st.currentId);
  let up = [];
  if (idx < 0) up = pl.slice(0, 5);
  else {
    for (let k = 1; k <= 5 && up.length < pl.length - 1; k++) {
      let j = idx + k;
      if (j >= pl.length) { if (!st.repeat) break; j %= pl.length; }
      up.push(pl[j]);
    }
  }
  if (up.length) {
    body.appendChild(el('div', 'q-sec', 'From VibeStream Mix'));
    up.forEach((s, k) => body.appendChild(item(s, '', k)));
  }
}

/* ---------- context menu ---------- */
function openMenu(anchor, song) {
  const m = $('menu');
  m.replaceChildren();
  const add = (label, command, cls) => {
    const b = el('button', cls || '', label);
    b.onclick = () => { closeMenu(); cmd(command); };
    m.appendChild(b);
  };
  add('Play next', 'playnext ' + song.id);
  add('Add to queue', 'q_add ' + song.id);
  add(isLiked(song.id) ? 'Remove from Liked Songs' : 'Save to Liked Songs', (isLiked(song.id) ? 'unlike ' : 'like ') + song.id);
  add('Delete from playlist', 'pl_delete ' + song.id, 'danger');
  m.hidden = false;
  const r = anchor.getBoundingClientRect();
  const w = m.offsetWidth, h = m.offsetHeight;
  m.style.left = Math.max(8, Math.min(r.right - w, innerWidth - w - 8)) + 'px';
  m.style.top = Math.max(8, Math.min(r.bottom + 4, innerHeight - h - 8)) + 'px';
}
function closeMenu() { $('menu').hidden = true; }
document.addEventListener('click', (e) => { if (!e.target.closest('#menu')) closeMenu(); });
$('main').addEventListener('scroll', closeMenu);

/* ---------- player ---------- */
function togglePlay() {
  if (!current()) { cmd('play'); return; }
  playing = !playing;
  render();
}

// Simulated playback clock: advances once a second; at the end it asks the engine for the next song.
function tick(advance) {
  const cur = current();
  if (advance && playing && cur) {
    elapsed++;
    if (elapsed >= cur.duration) { cmd('play'); return; }
  }
  const dur = cur ? cur.duration : 0;
  const pct = dur ? Math.min(100, (elapsed / dur) * 100) + '%' : '0%';
  ['', 'np-'].forEach((p) => {
    $(p ? 'np-cur' : 't-cur').textContent = mmss(Math.min(elapsed, dur));
    $(p ? 'np-total' : 't-total').textContent = mmss(dur);
    $(p ? 'np-fill' : 'fill').style.width = pct;
  });
}
setInterval(() => tick(true), 1000);

function seek(e) {
  const cur = current();
  if (!cur) return;
  const r = e.currentTarget.getBoundingClientRect();
  elapsed = Math.round(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * cur.duration);
  tick(false);
}
$('bar').onclick = seek;
$('np-bar').onclick = seek;

const goPrev = () => {
  if (elapsed > 3 || !st || st.history.length < 2) { elapsed = 0; tick(false); }
  else cmd('prev');
};
['b-play', 'np-play'].forEach((id) => { $(id).onclick = togglePlay; });
['b-next', 'np-next'].forEach((id) => { $(id).onclick = () => cmd('play'); });
['b-prev', 'np-prev'].forEach((id) => { $(id).onclick = goPrev; });
['b-repeat', 'np-repeat'].forEach((id) => { $(id).onclick = () => cmd('repeat'); });
$('btn-mode').onclick = () => cmd('mode');

/* ---------- navigation, queue drawer, Now Playing, search, add dialog ---------- */
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
  const np = $('np');
  np.classList.toggle('open', open);
  np.setAttribute('aria-hidden', String(!open));
}
$('p-left').onclick = () => toggleNP(true);
$('np-close').onclick = () => toggleNP(false);
$('np-queue').onclick = () => { toggleNP(false); toggleQueue(true); };

$('search').oninput = (e) => { filter = e.target.value; if (view !== 'home') setView('home'); else render(); };

$('add-cancel').onclick = () => $('add-dialog').close();
$('add-form').onsubmit = (e) => {
  e.preventDefault();
  const f = e.target;
  const clean = (v) => v.replace(/[|\r\n]/g, ' ').trim();   // '|' separates fields in the engine protocol
  const parts = [clean(f.title.value), clean(f.artist.value), f.duration.value];
  const pos = f.pos.value.trim();
  $('add-dialog').close();
  cmd(pos ? 'pl_insert ' + [pos, ...parts].join('|') : 'pl_add ' + parts.join('|'));
  f.reset();
};

document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') { closeMenu(); toggleNP(false); return; }
  if (e.code !== 'Space' || /INPUT|BUTTON|TEXTAREA/.test(e.target.tagName) || $('add-dialog').open) return;
  e.preventDefault();
  togglePlay();
});

cmd('state');
