// CRC32 implementation matching MosaicDB's C++ CRC32 (polynomial 0xEDB88320)
// This is a direct port of include/mosaicdb/common.h CRC32::compute()

const crcTable: number[] = [];
for (let i = 0; i < 256; i++) {
  let c = i;
  for (let j = 0; j < 8; j++) {
    c = (c & 1) ? ((c >>> 1) ^ 0xEDB88320) : (c >>> 1);
  }
  crcTable[i] = c >>> 0;
}

export function crc32(data: Uint8Array): number {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < data.length; i++) {
    crc = ((crc >>> 8) ^ crcTable[(crc ^ data[i]) & 0xFF]) >>> 0;
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

export function crc32Hex(data: Uint8Array): string {
  return crc32(data).toString(16).toUpperCase().padStart(8, '0');
}

// Build WAL payload bytes matching C++ WAL::append() format:
// [OpType(1)] [KeySize(4 LE)] [ValSize(4 LE)] [Key bytes] [Value bytes]
export function buildWALPayload(opType: 'PUT' | 'DELETE', key: string, value: string): Uint8Array {
  const keyBytes = new TextEncoder().encode(key);
  const valBytes = new TextEncoder().encode(value);
  const payloadSize = 1 + 4 + 4 + keyBytes.length + valBytes.length;
  const buf = new Uint8Array(payloadSize);
  const view = new DataView(buf.buffer);

  buf[0] = opType === 'PUT' ? 0x01 : 0x02;
  view.setUint32(1, keyBytes.length, true);
  view.setUint32(5, valBytes.length, true);
  buf.set(keyBytes, 9);
  buf.set(valBytes, 9 + keyBytes.length);

  return buf;
}

export function toHexArray(data: Uint8Array): string[] {
  return Array.from(data).map(b => b.toString(16).toUpperCase().padStart(2, '0'));
}

export function encodeU32LE(val: number): Uint8Array {
  const buf = new Uint8Array(4);
  new DataView(buf.buffer).setUint32(0, val, true);
  return buf;
}
