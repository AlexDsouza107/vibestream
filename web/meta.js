/* meta.js - what a local audio file can tell us about itself.
 *   parseFileName  "Artist - Title.mp3" -> { artist, title }
 *   readDuration   loads only the metadata of a blob: URL to get its length
 *   readID3        reads ID3v2 tags (title, artist, embedded cover art) from an MP3
 * No dependencies. Works in the browser (window.VSMeta) and in Node (for tests). */
(function (root) {
  const AUDIO_EXT = /\.(mp3|m4a|aac|wav|ogg|oga|opus|flac|weba)$/i;

  function isAudioFile(f) {
    return (f.type && f.type.startsWith('audio/')) || AUDIO_EXT.test(f.name || '');
  }

  /* Fallback metadata from the file name. "Artist - Title.mp3" is split; otherwise the artist is "Local Track". */
  function parseFileName(name) {
    const base = String(name).replace(/\.[^.]+$/, '').replace(/_/g, ' ').trim();
    const m = base.match(/^(.+?)\s+-\s+(.+)$/);
    if (m) return { artist: m[1].trim(), title: m[2].trim() };
    return { artist: 'Local Track', title: base || 'Untitled' };
  }

  /* Duration in seconds, using a detached Audio element (nothing is played). Rejects on unreadable files. */
  function readDuration(url) {
    return new Promise((resolve, reject) => {
      const a = new Audio();
      a.preload = 'metadata';
      const finish = (fn, v) => { clearTimeout(timer); a.onloadedmetadata = a.onerror = null; a.removeAttribute('src'); a.load(); fn(v); };
      const timer = setTimeout(() => finish(reject, new Error('Timed out reading audio')), 10000);
      a.onloadedmetadata = () => (isFinite(a.duration) && a.duration > 0 ? finish(resolve, a.duration) : finish(reject, new Error('Unknown duration')));
      a.onerror = () => finish(reject, new Error('Unreadable audio file'));
      a.src = url;
    });
  }

  /* ---------------- ID3v2 ---------------- */
  const u32 = (b, i) => ((b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]) >>> 0;
  const u24 = (b, i) => (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
  const synch = (b, i) => ((b[i] & 127) << 21) | ((b[i + 1] & 127) << 14) | ((b[i + 2] & 127) << 7) | (b[i + 3] & 127);
  const ascii = (b, i, n) => String.fromCharCode.apply(null, b.subarray(i, i + n));

  /* Text frame body -> string. First byte = encoding (0 latin1, 1 utf16+BOM, 2 utf16be, 3 utf8). */
  function decodeText(body, from) {
    const enc = body[0];
    const data = body.subarray(from === undefined ? 1 : from);
    let text;
    if (enc === 1 || enc === 2) {
      const be = enc === 2 || (data[0] === 0xfe && data[1] === 0xff);
      const start = data[0] === 0xff || data[0] === 0xfe ? 2 : 0;
      text = new TextDecoder(be ? 'utf-16be' : 'utf-16le').decode(data.subarray(start));
    } else {
      text = new TextDecoder(enc === 3 ? 'utf-8' : 'windows-1252').decode(data);
    }
    return text.split('\0')[0].trim();
  }

  /* Index of the end of a null-terminated string that starts at i (2-byte terminator for UTF-16). */
  function skipString(b, i, enc) {
    if (enc === 1 || enc === 2) {
      while (i + 1 < b.length && !(b[i] === 0 && b[i + 1] === 0)) i += 2;
      return i + 2;
    }
    while (i < b.length && b[i] !== 0) i++;
    return i + 1;
  }

  function sniffMime(d, fallback) {
    if (d[0] === 0xff && d[1] === 0xd8) return 'image/jpeg';
    if (d[0] === 0x89 && d[1] === 0x50) return 'image/png';
    if (d[0] === 0x47 && d[1] === 0x49) return 'image/gif';
    if (d[0] === 0x52 && d[1] === 0x49) return 'image/webp';
    return fallback;
  }

  /* APIC (v2.3/2.4) or PIC (v2.2) frame body -> { data, mime } */
  function parsePicture(body, v22) {
    const enc = body[0];
    let i = 1, mime;
    if (v22) { mime = 'image/' + ascii(body, 1, 3).toLowerCase().replace('jpg', 'jpeg'); i = 4; }
    else { const end = skipString(body, 1, 0); mime = ascii(body, 1, end - 2) || 'image/jpeg'; i = end; }
    i += 1;                                  // picture type byte
    i = skipString(body, i, enc);            // description
    const data = body.subarray(i);
    if (data.length < 8) return null;
    return { data, mime: sniffMime(data, mime.includes('/') ? mime : 'image/jpeg') };
  }

  /* Reverse "unsynchronisation": FF 00 -> FF */
  function unsync(b) {
    const out = new Uint8Array(b.length);
    let n = 0;
    for (let i = 0; i < b.length; i++) { out[n++] = b[i]; if (b[i] === 0xff && b[i + 1] === 0) i++; }
    return out.subarray(0, n);
  }

  /* Returns { title?, artist?, art?: {data, mime} }. Never throws on malformed tags. */
  async function readID3(file) {
    const out = {};
    try {
      const head = new Uint8Array(await file.slice(0, 10).arrayBuffer());
      if (ascii(head, 0, 3) !== 'ID3') return out;
      const ver = head[3], flags = head[5];
      if (ver < 2 || ver > 4) return out;
      const size = synch(head, 6);
      let buf = new Uint8Array(await file.slice(10, 10 + Math.min(size, 16 * 1024 * 1024)).arrayBuffer());
      if (flags & 0x80 && ver < 4) buf = unsync(buf);
      let pos = 0;
      if (flags & 0x40 && ver >= 3) pos = ver === 4 ? synch(buf, 0) : u32(buf, 0) + 4;   // skip extended header

      const hdr = ver === 2 ? 6 : 10;
      while (pos + hdr <= buf.length) {
        const id = ascii(buf, pos, ver === 2 ? 3 : 4);
        if (!/^[A-Z0-9]+$/.test(id)) break;                           // padding reached
        const fsize = ver === 2 ? u24(buf, pos + 3) : ver === 4 ? synch(buf, pos + 4) : u32(buf, pos + 4);
        const fflags = ver === 2 ? 0 : (buf[pos + 8] << 8) | buf[pos + 9];
        let body = buf.subarray(pos + hdr, pos + hdr + fsize);
        pos += hdr + fsize;
        if (!fsize) continue;
        const compressed = ver === 3 ? fflags & 0x80 : ver === 4 ? fflags & 0x08 : 0;
        const encrypted = ver === 3 ? fflags & 0x40 : ver === 4 ? fflags & 0x04 : 0;
        if (compressed || encrypted) continue;
        if (ver === 4 && fflags & 0x01) body = body.subarray(4);      // data length indicator

        if (id === 'TIT2' || id === 'TT2') out.title = out.title || decodeText(body);
        else if (id === 'TPE1' || id === 'TP1') out.artist = out.artist || decodeText(body);
        else if ((id === 'APIC' || id === 'PIC') && !out.art) out.art = parsePicture(body, id === 'PIC');
      }
    } catch (e) { /* unreadable tag: fall back to the file name */ }
    if (!out.title) delete out.title;
    if (!out.artist) delete out.artist;
    if (!out.art) delete out.art;
    return out;
  }

  const api = { isAudioFile, parseFileName, readDuration, readID3 };
  if (typeof module !== 'undefined' && module.exports) module.exports = api; else root.VSMeta = api;
})(typeof window !== 'undefined' ? window : globalThis);
