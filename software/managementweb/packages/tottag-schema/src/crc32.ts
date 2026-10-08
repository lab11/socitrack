// CRC-32 as the firmware and `zlib.crc32` both compute it.
//
// IEEE 802.3, reflected, polynomial 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF. The firmware
// computes this in software rather than on the Apollo4 SECURITY engine, because that engine requires
// a length that is a multiple of four and page payloads are record-aligned and therefore arbitrary.
//
// The choice of parameters is not incidental: it is what lets the Python dashboard verify a page with
// an unmodified `zlib.crc32` and this package verify the same page with the same answer. Any change
// here silently reclassifies every page in every archived file as corrupt.

const TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n += 1) {
    let c = n;
    for (let k = 0; k < 8; k += 1) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

/** CRC-32 over `bytes`, returned as an unsigned 32-bit number. */
export function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i += 1) {
    crc = TABLE[(crc ^ bytes[i]!) & 0xff]! ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
