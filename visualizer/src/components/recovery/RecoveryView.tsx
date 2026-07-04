import { useState, useRef, useCallback, useEffect } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { crc32Hex, buildWALPayload, toHexArray } from '../../utils/crc32';
import type { WALRecord, RecoveryPhase } from '../../types/database';

const COLORS = {
  cyan: '#38bdf8', green: '#10b981', red: '#ef4444',
  yellow: '#eab308', orange: '#f97316', surface: '#18181b',
  surface2: '#27272a', border: '#3f3f46', text: '#fafafa', dim: '#a1a1aa',
};

let nextRecordId = 0;

function createWALRecord(opType: 'PUT' | 'DELETE', key: string, value: string): WALRecord {
  const payload = buildWALPayload(opType, key, value);
  const checksum = crc32Hex(payload);
  const crcBytes = toHexArray(new Uint8Array([
    parseInt(checksum.slice(6, 8), 16), parseInt(checksum.slice(4, 6), 16),
    parseInt(checksum.slice(2, 4), 16), parseInt(checksum.slice(0, 2), 16),
  ]));
  const payloadHex = toHexArray(payload);
  return {
    id: nextRecordId++,
    crc32Stored: checksum,
    crc32Computed: checksum,
    opType, key, value,
    rawHex: [...crcBytes, ...payloadHex],
    isCorrupted: false,
    corruptedByteIndex: null,
    validationResult: 'pending',
  };
}

export default function RecoveryView() {
  const [phase, setPhase] = useState<RecoveryPhase>('active');
  const [memtable, setMemtable] = useState<{ key: string; value: string }[]>([]);
  const [walRecords, setWalRecords] = useState<WALRecord[]>([]);
  const [input, setInput] = useState('');
  const [cmdHistory, setCmdHistory] = useState<string[]>([]);
  const [scannerPos, setScannerPos] = useState(-1);
  const [isGlitching, setIsGlitching] = useState(false);
  const [flashRed, setFlashRed] = useState(false);
  const termRef = useRef<HTMLDivElement>(null);
  const walRef = useRef<HTMLDivElement>(null);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const submitCommand = useCallback((cmd: string) => {
    if (phase !== 'active') return;
    const parts = cmd.trim().split(/\s+/);
    const op = parts[0]?.toUpperCase();
    if (op === 'PUT' && parts.length >= 3) {
      const key = parts[1], value = parts.slice(2).join(' ');
      const rec = createWALRecord('PUT', key, value);
      setWalRecords(prev => [...prev, rec]);
      setMemtable(prev => {
        const filtered = prev.filter(e => e.key !== key);
        return [...filtered, { key, value }].sort((a, b) => a.key.localeCompare(b.key));
      });
      setCmdHistory(prev => [...prev, `> ${cmd}`, 'OK']);
    } else if (op === 'DELETE' && parts.length >= 2) {
      const key = parts[1];
      const rec = createWALRecord('DELETE', key, '');
      setWalRecords(prev => [...prev, rec]);
      setMemtable(prev => prev.filter(e => e.key !== key));
      setCmdHistory(prev => [...prev, `> ${cmd}`, 'OK (tombstone written)']);
    } else {
      setCmdHistory(prev => [...prev, `> ${cmd}`, 'ERROR: Use PUT key value or DELETE key']);
    }
  }, [phase]);

  const simulateCrash = useCallback(() => {
    setIsGlitching(true);
    setTimeout(() => {
      setPhase('crashed');
      setMemtable([]);
      setIsGlitching(false);
      setCmdHistory(prev => [...prev, '', '*** POWER LOSS — RAM CLEARED ***', '']);
    }, 600);
  }, []);

  const corruptByte = useCallback((recordIdx: number, byteIdx: number) => {
    if (phase !== 'active' && phase !== 'crashed') return;
    setWalRecords(prev => prev.map((rec, i) => {
      if (i !== recordIdx) return rec;
      const newHex = [...rec.rawHex];
      const original = parseInt(newHex[byteIdx], 16);
      const corrupted = (original ^ 0xFF) & 0xFF;
      newHex[byteIdx] = corrupted.toString(16).toUpperCase().padStart(2, '0');
      // Recompute CRC to detect corruption
      const payloadBytes = newHex.slice(4).map(h => parseInt(h, 16));
      const newComputedCrc = crc32Hex(new Uint8Array(payloadBytes));
      return { ...rec, rawHex: newHex, isCorrupted: true, corruptedByteIndex: byteIdx, crc32Computed: newComputedCrc };
    }));
  }, [phase]);

  const rebootRecover = useCallback(() => {
    setPhase('recovering');
    setScannerPos(0);
    setCmdHistory(prev => [...prev, '[ENGINE] Rebooting...', '[ENGINE] Scanning SSTable footers...', '[ENGINE] MAGIC 0x4D534454 ✓', '[ENGINE] Replaying WAL...']);
  }, []);

  // Recovery scanner animation
  useEffect(() => {
    if (phase !== 'recovering' || scannerPos < 0) return;
    if (scannerPos >= walRecords.length) {
      setPhase('recovered');
      setCmdHistory(prev => [...prev, `[ENGINE] Recovery complete. ${memtable.length} entries restored.`]);
      return;
    }
    timerRef.current = setTimeout(() => {
      const rec = walRecords[scannerPos];
      // Verify CRC
      const storedCrc = rec.rawHex.slice(0, 4).reverse().map(h => h).join('');
      const payloadBytes = rec.rawHex.slice(4).map(h => parseInt(h, 16));
      const computedCrc = crc32Hex(new Uint8Array(payloadBytes));
      const isValid = storedCrc.toUpperCase() === computedCrc.toUpperCase() && !rec.isCorrupted;

      setWalRecords(prev => prev.map((r, i) =>
        i === scannerPos ? { ...r, validationResult: isValid ? 'pass' : 'fail' } : r
      ));

      if (!isValid) {
        setFlashRed(true);
        setTimeout(() => setFlashRed(false), 500);
        setCmdHistory(prev => [...prev,
        `[WAL] Record #${scannerPos}: CRC32 MISMATCH!`,
        `[WAL]   Stored:   ${storedCrc.toUpperCase()}`,
        `[WAL]   Computed: ${computedCrc}`,
          `[WAL] ✗ Recovery HALTED — corrupt record discarded`,
        ]);
        setPhase('recovered');
        return;
      }

      // Restore entry to memtable
      if (rec.opType === 'PUT') {
        setMemtable(prev => {
          const filtered = prev.filter(e => e.key !== rec.key);
          return [...filtered, { key: rec.key, value: rec.value }].sort((a, b) => a.key.localeCompare(b.key));
        });
      } else {
        setMemtable(prev => prev.filter(e => e.key !== rec.key));
      }

      setCmdHistory(prev => [...prev, `[WAL] Record #${scannerPos}: CRC32 ${computedCrc} ✓ — ${rec.opType} ${rec.key}`]);
      setScannerPos(p => p + 1);
    }, 800);
    return () => { if (timerRef.current) clearTimeout(timerRef.current); };
  }, [phase, scannerPos, walRecords]);

  // Auto-scroll terminal
  useEffect(() => {
    if (termRef.current) termRef.current.scrollTop = termRef.current.scrollHeight;
  }, [cmdHistory]);

  const reset = () => {
    setPhase('active');
    setMemtable([]);
    setWalRecords([]);
    setCmdHistory([]);
    setScannerPos(-1);
    setInput('');
    nextRecordId = 0;
  };

  return (
    <div className={isGlitching ? 'glitch-active' : ''} style={{
      height: 'calc(100vh - 64px)', display: 'flex', padding: '20px', gap: '16px',
      ...(flashRed ? { border: `1px solid ${COLORS.red}`, boxShadow: `inset 0 0 0 1px ${COLORS.red}33` } : {}),
      transition: 'all 0.3s',
    }}>
      {/* LEFT: Engine Canvas */}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '12px' }}>
        {/* MemTable */}
        <div style={{
          padding: '16px', background: COLORS.surface, borderRadius: '12px',
          border: `1px solid ${COLORS.border}`, flex: 1, overflow: 'auto',
        }}>
          <h3 style={{ fontSize: '13px', fontWeight: 700, color: COLORS.dim, marginBottom: '10px', textTransform: 'uppercase', letterSpacing: '1px' }}>
            MemTable <span style={{ color: phase === 'crashed' ? COLORS.red : COLORS.cyan }}>(RAM{phase === 'crashed' ? ' — LOST' : ''})</span>
          </h3>
          <AnimatePresence mode="popLayout">
            {memtable.length === 0 ? (
              <motion.div key="empty" initial={{ opacity: 0 }} animate={{ opacity: 1 }} style={{ color: COLORS.dim, fontSize: '13px', fontFamily: 'JetBrains Mono' }}>
                {phase === 'crashed' ? '⚠ Memory cleared by power loss' : 'Empty — enter PUT commands →'}
              </motion.div>
            ) : memtable.map(entry => (
              <motion.div
                key={entry.key}
                layout
                initial={{ opacity: 0, x: -20 }}
                animate={{ opacity: 1, x: 0 }}
                exit={{ opacity: 0, scale: 0.5, filter: 'blur(8px)' }}
                transition={{ duration: 0.3 }}
                style={{
                  padding: '6px 10px', marginBottom: '3px', borderRadius: '4px', fontSize: '12px',
                  fontFamily: 'JetBrains Mono', background: COLORS.surface2, border: `1px solid ${COLORS.border}`,
                }}
              >
                <span style={{ color: COLORS.yellow }}>{entry.key}</span>
                <span style={{ color: COLORS.dim }}> → </span>
                <span style={{ color: COLORS.green }}>{entry.value}</span>
              </motion.div>
            ))}
          </AnimatePresence>
        </div>

        {/* WAL Viewer */}
        <div ref={walRef} style={{
          padding: '16px', background: COLORS.surface, borderRadius: '12px',
          border: `1px solid ${COLORS.border}`, flex: 2, overflow: 'auto',
        }}>
          <h3 style={{ fontSize: '13px', fontWeight: 700, color: COLORS.dim, marginBottom: '10px', textTransform: 'uppercase', letterSpacing: '1px' }}>
            WAL — wal.log <span style={{ color: COLORS.green }}>(Disk — survives crash)</span>
          </h3>
          {walRecords.length === 0 ? (
            <div style={{ color: COLORS.dim, fontSize: '13px', fontFamily: 'JetBrains Mono' }}>
              Empty — enter commands to fill WAL
            </div>
          ) : walRecords.map((rec, recIdx) => (
            <div key={rec.id} style={{
              marginBottom: '6px', padding: '8px', borderRadius: '6px',
              background: scannerPos === recIdx ? `${COLORS.cyan}1A` : rec.validationResult === 'fail' ? `${COLORS.red}1A` : rec.validationResult === 'pass' ? `${COLORS.green}11` : COLORS.surface2,
              border: `1px solid ${scannerPos === recIdx ? COLORS.cyan : rec.validationResult === 'fail' ? COLORS.red : COLORS.border}`,
              transition: 'all 0.3s',
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '4px' }}>
                <span style={{ fontSize: '11px', color: COLORS.dim }}>#{recIdx}</span>
                <span style={{ fontSize: '11px', color: rec.opType === 'PUT' ? COLORS.green : COLORS.red, fontWeight: 700 }}>
                  {rec.opType}
                </span>
                <span style={{ fontSize: '11px', fontFamily: 'JetBrains Mono', color: COLORS.yellow }}>{rec.key}</span>
                {rec.value && <span style={{ fontSize: '11px', fontFamily: 'JetBrains Mono', color: COLORS.dim }}> = {rec.value}</span>}
                <span style={{ marginLeft: 'auto' }}>
                  {rec.validationResult === 'pass' && <span style={{ color: COLORS.green }}>✓ CRC OK</span>}
                  {rec.validationResult === 'fail' && <span style={{ color: COLORS.red }}>✗ CRC FAIL</span>}
                </span>
              </div>
              {/* Hex dump */}
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '2px' }}>
                {rec.rawHex.map((byte, byteIdx) => {
                  const isCrc = byteIdx < 4;
                  const isOpType = byteIdx === 4;
                  const isKeySize = byteIdx >= 5 && byteIdx < 9;
                  const isValSize = byteIdx >= 9 && byteIdx < 13;
                  const isCorruptedByte = rec.corruptedByteIndex === byteIdx;
                  let color = COLORS.green;
                  if (isCrc) color = COLORS.orange;
                  else if (isOpType) color = COLORS.yellow;
                  else if (isKeySize || isValSize) color = COLORS.dim;
                  else color = COLORS.cyan;
                  if (isCorruptedByte) color = COLORS.red;

                  return (
                    <span
                      key={byteIdx}
                      onClick={() => corruptByte(recIdx, byteIdx)}
                      title={`Byte ${byteIdx}${isCrc ? ' (CRC32)' : isOpType ? ' (OpType)' : isKeySize ? ' (KeySize)' : isValSize ? ' (ValSize)' : ' (payload)'} — Click to corrupt`}
                      style={{
                        fontSize: '11px', fontFamily: 'JetBrains Mono', color,
                        padding: '1px 3px', borderRadius: '2px', cursor: 'pointer',
                        background: isCorruptedByte ? `${COLORS.red}22` : 'transparent',
                        border: `1px solid ${isCorruptedByte ? COLORS.red : 'transparent'}`,
                        transition: 'all 0.2s',
                      }}
                    >
                      {byte}
                    </span>
                  );
                })}
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* RIGHT: Terminal + Controls */}
      <div style={{ width: '380px', display: 'flex', flexDirection: 'column', gap: '12px' }}>
        {/* Action Buttons */}
        <div style={{ display: 'flex', gap: '8px' }}>
          {phase === 'active' && (
            <button onClick={simulateCrash} disabled={walRecords.length === 0}
              style={{
                flex: 1, padding: '10px 14px', borderRadius: '8px', fontSize: '13px', fontWeight: 600,
                background: walRecords.length === 0 ? COLORS.surface2 : COLORS.red,
                color: walRecords.length === 0 ? COLORS.dim : '#fff',
                border: 'none',
                cursor: walRecords.length === 0 ? 'not-allowed' : 'pointer',
                fontFamily: 'var(--font-sans)', transition: 'all 0.2s',
              }}
            >
              Simulate Power Loss
            </button>
          )}
          {phase === 'crashed' && (
            <button onClick={rebootRecover}
              style={{
                flex: 1, padding: '10px 14px', borderRadius: '8px', fontSize: '13px', fontWeight: 600,
                background: COLORS.cyan,
                color: '#000', border: 'none',
                cursor: 'pointer',
                fontFamily: 'var(--font-sans)', transition: 'all 0.2s',
              }}
            >
              🔄 Reboot & Recover
            </button>
          )}
          {(phase === 'recovered' || phase === 'active') && (
            <button onClick={reset} style={{
              padding: '10px 16px', borderRadius: '8px', fontSize: '13px', fontWeight: 500,
              background: 'transparent', color: COLORS.dim,
              border: `1px solid ${COLORS.border}`, cursor: 'pointer',
              fontFamily: 'var(--font-sans)', transition: 'all 0.2s',
            }}>↻ Reset</button>
          )}
        </div>

        {/* Terminal */}
        <div style={{
          flex: 1, background: '#0d0d0d', borderRadius: '12px', border: `1px solid ${COLORS.border}`,
          display: 'flex', flexDirection: 'column', overflow: 'hidden',
        }}>
          <div style={{
            padding: '10px 14px', background: '#1a1a1a', borderBottom: `1px solid ${COLORS.border}`,
            display: 'flex', alignItems: 'center', gap: '8px',
          }}>
            <div style={{ width: '10px', height: '10px', borderRadius: '50%', background: phase === 'active' ? COLORS.green : COLORS.red }} />
            <span style={{ fontSize: '12px', fontWeight: 600, color: COLORS.dim }}>
              MosaicDB Client {phase !== 'active' ? '(disconnected)' : ''}
            </span>
          </div>

          <div ref={termRef} style={{ flex: 1, padding: '12px', overflow: 'auto', fontFamily: 'JetBrains Mono', fontSize: '12px' }}>
            <div style={{ color: COLORS.green, marginBottom: '8px' }}>
              MosaicDB Terminal v0.2.0{'\n'}
              Commands: PUT key value | DELETE key{'\n'}
              Tip: Click WAL bytes to corrupt them before crashing!
            </div>
            {cmdHistory.map((line, i) => (
              <div key={i} style={{
                color: line.startsWith('>') ? COLORS.text :
                  line.includes('✓') ? COLORS.green :
                    line.includes('✗') || line.includes('MISMATCH') || line.includes('HALTED') ? COLORS.red :
                      line.includes('[ENGINE]') || line.includes('[WAL]') ? COLORS.cyan :
                        line.includes('***') ? COLORS.red :
                          COLORS.dim,
                fontWeight: line.includes('***') ? 700 : 400,
              }}>
                {line}
              </div>
            ))}
            {phase === 'recovering' && (
              <div style={{ color: COLORS.cyan }}>
                Scanning WAL record #{scannerPos}...
              </div>
            )}
          </div>

          {phase === 'active' && (
            <form onSubmit={(e) => { e.preventDefault(); if (input.trim()) { submitCommand(input); setInput(''); } }}
              style={{ padding: '8px 12px', borderTop: `1px solid ${COLORS.border}`, display: 'flex', gap: '8px' }}>
              <span style={{ color: COLORS.green, fontFamily: 'JetBrains Mono', fontSize: '12px' }}>$</span>
              <input
                value={input}
                onChange={e => setInput(e.target.value)}
                placeholder="PUT user:alice 30"
                autoFocus
                style={{
                  flex: 1, background: 'transparent', border: 'none', outline: 'none',
                  color: COLORS.text, fontFamily: 'JetBrains Mono', fontSize: '12px',
                }}
              />
            </form>
          )}
        </div>

        {/* Info cards */}
        <div style={{
          padding: '14px', background: COLORS.surface, borderRadius: '12px',
          border: `1px solid ${COLORS.border}`, fontSize: '12px',
        }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
            <span style={{ color: COLORS.dim }}>Phase</span>
            <span style={{
              color: phase === 'active' ? COLORS.green : phase === 'crashed' ? COLORS.red : phase === 'recovering' ? COLORS.cyan : COLORS.green,
              fontWeight: 700, textTransform: 'uppercase',
            }}>{phase}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
            <span style={{ color: COLORS.dim }}>MemTable entries</span>
            <span style={{ color: COLORS.text, fontFamily: 'JetBrains Mono' }}>{memtable.length}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '6px' }}>
            <span style={{ color: COLORS.dim }}>WAL records</span>
            <span style={{ color: COLORS.text, fontFamily: 'JetBrains Mono' }}>{walRecords.length}</span>
          </div>
          <div style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span style={{ color: COLORS.dim }}>Corrupted bytes</span>
            <span style={{ color: walRecords.some(r => r.isCorrupted) ? COLORS.red : COLORS.text, fontFamily: 'JetBrains Mono' }}>
              {walRecords.filter(r => r.isCorrupted).length}
            </span>
          </div>
        </div>
      </div>
    </div>
  );
}
