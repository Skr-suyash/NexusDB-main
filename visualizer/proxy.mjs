// MosaicDB TCP-to-HTTP Proxy
// Bridges browser HTTP requests to the MosaicDB TCP server (PROTO_SQL)
// Run: node proxy.mjs

import http from 'http';
import net from 'net';

const MOSAIC_HOST = '127.0.0.1';
const MOSAIC_PORT = 7690;
const PROXY_PORT = 3001;
const PROTO_SQL = 0x05;

function encodeU32LE(val) {
  const buf = Buffer.alloc(4);
  buf.writeUInt32LE(val, 0);
  return buf;
}

function sendSQL(sql) {
  return new Promise((resolve, reject) => {
    const client = new net.Socket();
    client.setTimeout(5000);

    client.connect(MOSAIC_PORT, MOSAIC_HOST, () => {
      // PROTO_SQL: header = [type(1)] [key_size(4)] [val_size(4)] + key_data
      // key = SQL string, val = empty
      const sqlBuf = Buffer.from(sql, 'utf-8');
      const header = Buffer.alloc(9);
      header[0] = PROTO_SQL;
      header.writeUInt32LE(sqlBuf.length, 1);
      header.writeUInt32LE(0, 5);
      client.write(Buffer.concat([header, sqlBuf]));
    });

    let respBuf = Buffer.alloc(0);
    let headerParsed = false;
    let expectedSize = 0;
    let respStatus = 0;

    client.on('data', (chunk) => {
      respBuf = Buffer.concat([respBuf, chunk]);

      if (!headerParsed && respBuf.length >= 5) {
        respStatus = respBuf[0];
        expectedSize = respBuf.readUInt32LE(1);
        headerParsed = true;
      }

      if (headerParsed && respBuf.length >= 5 + expectedSize) {
        const payload = respBuf.subarray(5, 5 + expectedSize);
        client.destroy();

        try {
          const result = parseResponse(respStatus, payload);
          resolve(result);
        } catch (e) {
          resolve({ ok: false, message: 'Parse error: ' + e.message, columns: [], rows: [] });
        }
      }
    });

    client.on('error', (err) => {
      reject(new Error('Cannot connect to MosaicDB server: ' + err.message));
    });

    client.on('timeout', () => {
      client.destroy();
      reject(new Error('Connection to MosaicDB server timed out'));
    });
  });
}

function parseResponse(status, payload) {
  const result = {
    ok: status === 0x00,
    message: '',
    columns: [],
    rows: [],
  };

  if (payload.length < 4) {
    result.message = payload.toString('utf-8');
    return result;
  }

  let pos = 0;
  const readU32 = () => { const v = payload.readUInt32LE(pos); pos += 4; return v; };
  const readStr = (len) => { const s = payload.subarray(pos, pos + len).toString('utf-8'); pos += len; return s; };

  // Message
  const msgLen = readU32();
  result.message = readStr(msgLen);

  // Columns
  const colCount = readU32();
  for (let i = 0; i < colCount; i++) {
    const clen = readU32();
    result.columns.push(readStr(clen));
  }

  // Rows
  const rowCount = readU32();
  for (let i = 0; i < rowCount; i++) {
    const row = [];
    for (let j = 0; j < colCount; j++) {
      const vlen = readU32();
      row.push(readStr(vlen));
    }
    result.rows.push(row);
  }

  return result;
}

const server = http.createServer(async (req, res) => {
  // CORS headers
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  if (req.method === 'POST' && req.url === '/api/sql') {
    let body = '';
    req.on('data', (chunk) => { body += chunk; });
    req.on('end', async () => {
      try {
        const { sql } = JSON.parse(body);
        if (!sql) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, message: 'Missing "sql" field' }));
          return;
        }
        const result = await sendSQL(sql);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(result));
      } catch (e) {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, message: e.message, columns: [], rows: [] }));
      }
    });
  } else if (req.method === 'GET' && req.url === '/api/health') {
    // Check if MosaicDB server is reachable
    try {
      await sendSQL('SHOW TABLES');
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'connected' }));
    } catch {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ status: 'disconnected' }));
    }
  } else {
    res.writeHead(404);
    res.end('Not found');
  }
});

server.listen(PROXY_PORT, () => {
  console.log(`MosaicDB HTTP Proxy running on http://localhost:${PROXY_PORT}`);
  console.log(`Forwarding SQL to MosaicDB TCP server at ${MOSAIC_HOST}:${MOSAIC_PORT}`);
  console.log(`Make sure mosaicdb_server.exe is running!`);
});
