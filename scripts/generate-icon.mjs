// Generates a 1024x1024 PNG: indigo background with a white "N".
// No image deps — emits a valid PNG via zlib + a hand-rolled CRC32.
import { deflateSync } from "node:zlib";
import { writeFileSync } from "node:fs";

const S = 1024;
const bg = [124, 140, 255, 255]; // --accent #7c8cff
const fg = [255, 255, 255, 255];

function inN(x, y) {
  const top = 220, bot = 804;
  if (y < top || y > bot) return false;
  // vertical bars
  if (x >= 200 && x < 320) return true;
  if (x >= 704 && x < 824) return true;
  // diagonal stroke
  const cx = 320 + ((704 - 320) * (y - top)) / (bot - top);
  return Math.abs(x - cx) < 72;
}

// Build raw RGBA scanlines, each prefixed with filter byte 0.
const raw = Buffer.alloc((S * 4 + 1) * S);
let o = 0;
for (let y = 0; y < S; y++) {
  raw[o++] = 0;
  for (let x = 0; x < S; x++) {
    const c = inN(x, y) ? fg : bg;
    raw[o++] = c[0];
    raw[o++] = c[1];
    raw[o++] = c[2];
    raw[o++] = c[3];
  }
}

const crcTable = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length, 0);
  const typeBuf = Buffer.from(type, "ascii");
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([typeBuf, data])), 0);
  return Buffer.concat([len, typeBuf, data, crc]);
}

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(S, 0);
ihdr.writeUInt32BE(S, 4);
ihdr[8] = 8; // bit depth
ihdr[9] = 6; // color type RGBA
ihdr[10] = 0;
ihdr[11] = 0;
ihdr[12] = 0;

const png = Buffer.concat([
  Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
  chunk("IHDR", ihdr),
  chunk("IDAT", deflateSync(raw, { level: 9 })),
  chunk("IEND", Buffer.alloc(0)),
]);

writeFileSync(process.argv[2] || "icon-source.png", png);
console.log("wrote", process.argv[2] || "icon-source.png");
