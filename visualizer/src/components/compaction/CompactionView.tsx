import { useState, useCallback, useRef, useEffect } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { generateMockSSTables, runMockCompaction } from '../../utils/mockData';
import type { SSTableMeta, KVEntry, CompactionMetrics } from '../../types/database';
import type { CompactionStep } from '../../utils/mockData';

const COLORS = {
  cyan: '#38bdf8', green: '#10b981', red: '#ef4444',
  yellow: '#eab308', orange: '#f97316', surface: '#18181b',
  surface2: '#27272a', border: '#3f3f46', text: '#fafafa', dim: '#a1a1aa',
};

export default function CompactionView() {
  const [tables] = useState<SSTableMeta[]>(generateMockSSTables);
  const [steps, setSteps] = useState<CompactionStep[]>([]);
  const [currentStep, setCurrentStep] = useState(-1);
  const [outputEntries, setOutputEntries] = useState<KVEntry[]>([]);
  const [highlightKey, setHighlightKey] = useState<string | null>(null);
  const [removedKeys, setRemovedKeys] = useState<Set<string>>(new Set());
  const [metrics, setMetrics] = useState<CompactionMetrics>({
    keysProcessed: 0, duplicatesOverwritten: 0, tombstonesPurged: 0, inputBytes: 0, outputBytes: 0,
  });
  const [isRunning, setIsRunning] = useState(false);
  const [speed, setSpeed] = useState(800);
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const startCompaction = useCallback(() => {
    const s = runMockCompaction(tables);
    setSteps(s);
    setCurrentStep(0);
    setOutputEntries([]);
    setHighlightKey(null);
    setRemovedKeys(new Set());
    setMetrics({ keysProcessed: 0, duplicatesOverwritten: 0, tombstonesPurged: 0, inputBytes: 0, outputBytes: 0 });
    setIsRunning(true);
  }, [tables]);

  const processStep = useCallback((step: CompactionStep) => {
    if (step.type === 'compare') {
      setHighlightKey(step.key);
      setMetrics(m => ({ ...m, keysProcessed: m.keysProcessed + 1 }));
    } else if (step.type === 'keep' && step.winner) {
      setOutputEntries(prev => [...prev, step.winner!]);
      setHighlightKey(null);
    } else if (step.type === 'overwrite' && step.winner) {
      setOutputEntries(prev => [...prev, step.winner!]);
      setMetrics(m => ({ ...m, duplicatesOverwritten: m.duplicatesOverwritten + (step.losers?.length || 0) }));
      setRemovedKeys(prev => {
        const n = new Set(prev);
        step.losers?.forEach(l => n.add(l.key + ':' + l.sequenceNumber));
        return n;
      });
      setHighlightKey(null);
    } else if (step.type === 'tombstone_purge') {
      setMetrics(m => ({ ...m, tombstonesPurged: m.tombstonesPurged + 1 + (step.losers?.length || 0) }));
      setRemovedKeys(prev => {
        const n = new Set(prev);
        if (step.winner) n.add(step.winner.key + ':' + step.winner.sequenceNumber);
        step.losers?.forEach(l => n.add(l.key + ':' + l.sequenceNumber));
        return n;
      });
      setHighlightKey(null);
    } else if (step.type === 'done') {
      setIsRunning(false);
      setHighlightKey(null);
    }
  }, []);

  useEffect(() => {
    if (!isRunning || currentStep < 0 || currentStep >= steps.length) return;
    timerRef.current = setTimeout(() => {
      processStep(steps[currentStep]);
      setCurrentStep(prev => prev + 1);
    }, speed);
    return () => { if (timerRef.current) clearTimeout(timerRef.current); };
  }, [isRunning, currentStep, steps, speed, processStep]);

  const reset = () => {
    setIsRunning(false);
    setCurrentStep(-1);
    setOutputEntries([]);
    setHighlightKey(null);
    setRemovedKeys(new Set());
    setSteps([]);
    setMetrics({ keysProcessed: 0, duplicatesOverwritten: 0, tombstonesPurged: 0, inputBytes: 0, outputBytes: 0 });
    if (timerRef.current) clearTimeout(timerRef.current);
  };

  const isDone = currentStep >= steps.length && steps.length > 0;

  return (
    <div style={{ height: 'calc(100vh - 64px)', display: 'flex', flexDirection: 'column', padding: '20px', gap: '16px' }}>
      {/* Controls */}
      <div style={{
        display: 'flex', alignItems: 'center', gap: '12px', padding: '12px 20px',
        background: COLORS.surface, borderRadius: '12px', border: `1px solid ${COLORS.border}`,
      }}>
        <button onClick={startCompaction} disabled={isRunning} style={btnStyle(COLORS.cyan, isRunning)}>
          ▶ Start Compaction
        </button>
        <button onClick={reset} style={btnStyle(COLORS.orange, false)}>↻ Reset</button>
        <div style={{ marginLeft: 'auto', display: 'flex', alignItems: 'center', gap: '8px', color: COLORS.dim, fontSize: '13px' }}>
          <span>Speed</span>
          <input type="range" min="100" max="2000" step="100" value={speed}
            onChange={e => setSpeed(Number(e.target.value))}
            style={{ width: '120px', accentColor: COLORS.cyan }} />
          <span style={{ fontFamily: 'JetBrains Mono', color: COLORS.text, width: '50px' }}>{speed}ms</span>
        </div>
      </div>

      {/* Main visualization area */}
      <div style={{ flex: 1, display: 'flex', gap: '16px', overflow: 'hidden' }}>
        {/* Left: Source SSTables + Merge + Output */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '12px', overflow: 'auto' }}>
          {/* Source SSTables */}
          <div style={{ padding: '16px', background: COLORS.surface, borderRadius: '12px', border: `1px solid ${COLORS.border}` }}>
            <h3 style={{ fontSize: '13px', fontWeight: 700, color: COLORS.dim, marginBottom: '12px', textTransform: 'uppercase', letterSpacing: '1px' }}>
              Source SSTables ({tables.length} files)
            </h3>
            <div style={{ display: 'grid', gridTemplateColumns: `repeat(${tables.length}, 1fr)`, gap: '8px' }}>
              {tables.map((table, idx) => (
                <div key={table.id} style={{
                  background: COLORS.surface2, borderRadius: '8px', padding: '10px',
                  border: `1px solid ${COLORS.border}`, opacity: isDone ? 0.3 : 1,
                  transition: 'opacity 0.5s',
                }}>
                  <div style={{ fontSize: '11px', fontFamily: 'JetBrains Mono', color: COLORS.cyan, marginBottom: '8px', fontWeight: 700 }}>
                    {table.id} <span style={{ color: COLORS.dim }}>seq={idx}</span>
                  </div>
                  <div style={{ display: 'flex', flexDirection: 'column', gap: '3px' }}>
                    {table.entries.map(entry => {
                      const uid = entry.key + ':' + entry.sequenceNumber;
                      const isActive = highlightKey === entry.key;
                      const isRemoved = removedKeys.has(uid);
                      return (
                        <AnimatePresence key={uid}>
                          {!isRemoved && (
                            <motion.div
                              initial={{ opacity: 1, scale: 1 }}
                              exit={{ opacity: 0, scale: 0.5, filter: 'blur(10px)' }}
                              transition={{ duration: 0.4 }}
                              style={{
                                padding: '4px 8px', borderRadius: '4px', fontSize: '11px',
                                fontFamily: 'JetBrains Mono',
                                background: isActive ? `${COLORS.cyan}1A` : entry.isTombstone ? `${COLORS.red}11` : `${COLORS.surface2}`,
                                border: `1px solid ${isActive ? COLORS.cyan : entry.isTombstone ? COLORS.red + '44' : COLORS.border}`,
                                color: entry.isTombstone ? COLORS.red : COLORS.text,
                                textDecoration: entry.isTombstone ? 'line-through' : 'none',
                                transition: 'all 0.3s',
                              }}
                            >
                              <span style={{ color: COLORS.yellow }}>{entry.key}</span>
                              {entry.isTombstone ? ' ' :
                                <span style={{ color: COLORS.dim }}> = {(entry.value || '').substring(0, 20)}{(entry.value || '').length > 20 ? '…' : ''}</span>
                              }
                            </motion.div>
                          )}
                        </AnimatePresence>
                      );
                    })}
                  </div>
                </div>
              ))}
            </div>
          </div>

          {/* Merge Core */}
          <AnimatePresence>
            {highlightKey && (
              <motion.div
                initial={{ opacity: 0, y: -10 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: 10 }}
                style={{
                  padding: '16px', background: `linear-gradient(135deg, #00f0ff08, #00ff8808)`,
                  borderRadius: '12px', border: `1px solid ${COLORS.cyan}33`,
                  textAlign: 'center',
                }}
              >
                <div style={{ fontSize: '11px', color: COLORS.dim, marginBottom: '6px', textTransform: 'uppercase', letterSpacing: '1px' }}>
                  Merging Key
                </div>
                <div style={{
                  fontSize: '16px', fontWeight: 600, fontFamily: 'JetBrains Mono', color: COLORS.cyan,
                }}>
                  {highlightKey}
                </div>
              </motion.div>
            )}
          </AnimatePresence>

          {/* Output SSTable */}
          <div style={{
            padding: '16px', background: COLORS.surface, borderRadius: '12px',
            border: `1px solid ${isDone ? COLORS.green : COLORS.border}`,
            transition: 'all 0.5s', flex: 1, overflow: 'auto',
          }}>
            <h3 style={{ fontSize: '13px', fontWeight: 700, color: isDone ? COLORS.green : COLORS.dim, marginBottom: '12px', textTransform: 'uppercase', letterSpacing: '1px' }}>
              {isDone ? '✓ Output SSTable (sst_000004)' : 'Output SSTable (building...)'}
            </h3>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '3px' }}>
              <AnimatePresence>
                {outputEntries.map((entry, i) => (
                  <motion.div
                    key={entry.key + i}
                    initial={{ opacity: 0, x: -20 }}
                    animate={{ opacity: 1, x: 0 }}
                    transition={{ duration: 0.3 }}
                    style={{
                      padding: '4px 8px', borderRadius: '4px', fontSize: '11px',
                      fontFamily: 'JetBrains Mono', background: `${COLORS.green}11`,
                      border: `1px solid ${COLORS.green}33`,
                    }}
                  >
                    <span style={{ color: COLORS.green }}>✓</span>{' '}
                    <span style={{ color: COLORS.yellow }}>{entry.key}</span>
                    <span style={{ color: COLORS.dim }}> = {(entry.value || '').substring(0, 30)}</span>
                  </motion.div>
                ))}
              </AnimatePresence>
            </div>
          </div>
        </div>

        {/* Right: Metrics Dashboard */}
        <div style={{ width: '220px', display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <MetricCard label="Keys Processed" value={metrics.keysProcessed} color={COLORS.cyan} />
          <MetricCard label="Duplicates Overwritten" value={metrics.duplicatesOverwritten} color={COLORS.yellow} />
          <MetricCard label="Tombstones Purged" value={metrics.tombstonesPurged} color={COLORS.red} />
          <MetricCard label="Output Entries" value={outputEntries.length} color={COLORS.green} />

          {/* Legend */}
          <div style={{ marginTop: 'auto', padding: '16px', background: COLORS.surface, borderRadius: '12px', border: `1px solid ${COLORS.border}` }}>
            <h4 style={{ fontSize: '11px', color: COLORS.dim, marginBottom: '8px', textTransform: 'uppercase', letterSpacing: '1px' }}>Legend</h4>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px', fontSize: '12px' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <div style={{ width: '12px', height: '12px', borderRadius: '2px', background: COLORS.cyan + '33', border: `1px solid ${COLORS.cyan}` }} />
                <span style={{ color: COLORS.text }}>Active comparison</span>
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <div style={{ width: '12px', height: '12px', borderRadius: '2px', background: COLORS.red + '33', border: `1px solid ${COLORS.red}` }} />
                <span style={{ color: COLORS.text }}>Tombstone (deleted)</span>
              </div>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                <div style={{ width: '12px', height: '12px', borderRadius: '2px', background: COLORS.green + '33', border: `1px solid ${COLORS.green}` }} />
                <span style={{ color: COLORS.text }}>Written to output</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

function MetricCard({ label, value, color }: { label: string; value: number; color: string }) {
  return (
    <div style={{
      padding: '16px', background: COLORS.surface, borderRadius: '12px',
      border: `1px solid ${COLORS.border}`,
    }}>
      <div style={{ fontSize: '11px', color: '#666680', marginBottom: '6px', textTransform: 'uppercase', letterSpacing: '1px' }}>
        {label}
      </div>
      <motion.div
        key={value}
        initial={{ scale: 1.3, color }}
        animate={{ scale: 1, color }}
        style={{ fontSize: '28px', fontWeight: 800, fontFamily: 'JetBrains Mono' }}
      >
        {value}
      </motion.div>
    </div>
  );
}

function btnStyle(color: string, disabled: boolean): React.CSSProperties {
  return {
    padding: '8px 18px', borderRadius: '8px', border: `1px solid ${disabled ? COLORS.border : color + '44'}`,
    background: disabled ? COLORS.surface2 : `${color}11`, color: disabled ? COLORS.dim : color,
    fontSize: '13px', fontWeight: 500, cursor: disabled ? 'not-allowed' : 'pointer',
    fontFamily: 'var(--font-sans)', transition: 'all 0.15s ease',
  };
}
