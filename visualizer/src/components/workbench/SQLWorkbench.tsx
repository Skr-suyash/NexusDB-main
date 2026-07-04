import { useState, useRef, useEffect, useCallback } from 'react';
import { motion, AnimatePresence } from 'framer-motion';

const COLORS = {
  cyan: '#38bdf8', green: '#10b981', red: '#ef4444',
  yellow: '#eab308', orange: '#f97316', surface: '#18181b',
  surface2: '#27272a', border: '#3f3f46', text: '#fafafa', dim: '#a1a1aa',
};

const PROXY_URL = 'http://localhost:3001';

interface QueryResult {
  ok: boolean;
  message: string;
  columns: string[];
  rows: string[][];
}

interface HistoryEntry {
  sql: string;
  result: QueryResult;
  timestamp: Date;
  duration: number;
}

const SAMPLE_QUERIES = [
  "CREATE TABLE employees (id INT PRIMARY KEY, name TEXT NOT NULL, salary FLOAT, dept TEXT)",
  "INSERT INTO employees (id, name, salary, dept) VALUES (1, 'Alice', 95000.00, 'Engineering')",
  "INSERT INTO employees (id, name, salary, dept) VALUES (2, 'Bob', 82000.00, 'Marketing')",
  "INSERT INTO employees (id, name, salary, dept) VALUES (3, 'Charlie', 110000.00, 'Engineering')",
  "INSERT INTO employees (id, name, salary, dept) VALUES (4, 'Diana', 78000.00, 'Design')",
  "SELECT * FROM employees",
  "SELECT name, salary FROM employees WHERE salary > 85000 ORDER BY salary DESC",
  "UPDATE employees SET salary = 100000.00 WHERE id = 2",
  "SHOW TABLES",
  "DESCRIBE employees",
];

export default function SQLWorkbench() {
  const [sql, setSql] = useState('');
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [isExecuting, setIsExecuting] = useState(false);
  const [connected, setConnected] = useState<boolean | null>(null);
  const [activeResult, setActiveResult] = useState<HistoryEntry | null>(null);
  const editorRef = useRef<HTMLTextAreaElement>(null);
  const resultsRef = useRef<HTMLDivElement>(null);

  // Check connection on mount
  useEffect(() => {
    checkConnection();
    const interval = setInterval(checkConnection, 30000);
    return () => clearInterval(interval);
  }, []);

  const checkConnection = async () => {
    try {
      const res = await fetch(`${PROXY_URL}/api/health`);
      const data = await res.json();
      setConnected(data.status === 'connected');
    } catch {
      setConnected(false);
    }
  };

  const executeSQL = useCallback(async (query?: string) => {
    const sqlToRun = (query || sql).trim();
    if (!sqlToRun || isExecuting) return;

    setIsExecuting(true);
    const start = performance.now();

    try {
      const res = await fetch(`${PROXY_URL}/api/sql`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sql: sqlToRun }),
      });
      const result: QueryResult = await res.json();
      const duration = performance.now() - start;

      const entry: HistoryEntry = { sql: sqlToRun, result, timestamp: new Date(), duration };
      setHistory(prev => [entry, ...prev]);
      setActiveResult(entry);
      if (!query) setSql('');
    } catch (e: any) {
      const duration = performance.now() - start;
      const entry: HistoryEntry = {
        sql: sqlToRun,
        result: { ok: false, message: `Connection error: ${e.message}. Is the proxy running?`, columns: [], rows: [] },
        timestamp: new Date(), duration,
      };
      setHistory(prev => [entry, ...prev]);
      setActiveResult(entry);
    } finally {
      setIsExecuting(false);
    }
  }, [sql, isExecuting]);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      executeSQL();
    }
  };

  return (
    <div style={{ height: 'calc(100vh - 64px)', display: 'flex', padding: '20px', gap: '16px' }}>
      {/* Left: Editor + Results */}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: '12px' }}>
        {/* Connection status + controls */}
        <div style={{
          display: 'flex', alignItems: 'center', gap: '12px', padding: '10px 16px',
          background: COLORS.surface, borderRadius: '10px', border: `1px solid ${COLORS.border}`,
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
            <div style={{
              width: '8px', height: '8px', borderRadius: '50%',
              background: connected === true ? COLORS.green : connected === false ? COLORS.red : COLORS.yellow,
              boxShadow: connected === true ? `0 0 8px ${COLORS.green}` : 'none',
            }} />
            <span style={{ fontSize: '12px', color: COLORS.dim }}>
              {connected === true ? 'Connected to MosaicDB' : connected === false ? 'Disconnected' : 'Checking...'}
            </span>
          </div>
          <span style={{ fontSize: '11px', color: COLORS.dim, fontFamily: 'JetBrains Mono' }}>localhost:7690</span>
          <div style={{ marginLeft: 'auto', display: 'flex', gap: '6px' }}>
            <button onClick={() => executeSQL()} disabled={isExecuting || !sql.trim()}
              style={{
                padding: '6px 16px', borderRadius: '6px', fontSize: '12px', fontWeight: 600,
                background: isExecuting || !sql.trim() ? COLORS.surface2 : COLORS.green,
                color: isExecuting || !sql.trim() ? COLORS.dim : '#000',
                border: 'none',
                cursor: isExecuting || !sql.trim() ? 'not-allowed' : 'pointer',
                fontFamily: 'var(--font-sans)',
              }}
            >
              {isExecuting ? '⏳ Running...' : '▶ Execute'} <span style={{ fontSize: '10px', opacity: 0.6 }}>Ctrl+Enter</span>
            </button>
          </div>
        </div>

        {/* SQL Editor */}
        <div style={{
          background: '#0d0d0d', borderRadius: '10px', border: `1px solid ${COLORS.border}`,
          overflow: 'hidden',
        }}>
          <div style={{
            padding: '8px 14px', background: '#1a1a1a', borderBottom: `1px solid ${COLORS.border}`,
            fontSize: '11px', color: COLORS.dim, display: 'flex', alignItems: 'center', justifyContent: 'space-between',
          }}>
            <span>SQL Editor</span>
            <span style={{ fontFamily: 'JetBrains Mono', fontSize: '10px' }}>
              {sql.length} chars
            </span>
          </div>
          <textarea
            ref={editorRef}
            value={sql}
            onChange={e => setSql(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="Enter SQL query here...&#10;&#10;Examples:&#10;  CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT)&#10;  INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30)&#10;  SELECT * FROM users WHERE age > 25&#10;  SHOW TABLES"
            spellCheck={false}
            style={{
              width: '100%', minHeight: '140px', padding: '12px 14px', resize: 'vertical',
              background: 'transparent', border: 'none', outline: 'none',
              color: COLORS.cyan, fontFamily: 'JetBrains Mono', fontSize: '13px',
              lineHeight: '1.6',
            }}
          />
        </div>

        {/* Results */}
        <div ref={resultsRef} style={{
          flex: 1, background: COLORS.surface, borderRadius: '10px',
          border: `1px solid ${COLORS.border}`, overflow: 'auto', display: 'flex', flexDirection: 'column',
        }}>
          <div style={{
            padding: '8px 14px', borderBottom: `1px solid ${COLORS.border}`,
            fontSize: '11px', color: COLORS.dim, display: 'flex', alignItems: 'center', justifyContent: 'space-between',
            flexShrink: 0,
          }}>
            <span>Results</span>
            {activeResult && (
              <span style={{ fontFamily: 'JetBrains Mono', fontSize: '10px', color: activeResult.result.ok ? COLORS.green : COLORS.red }}>
                {activeResult.result.ok ? '✓' : '✗'} {activeResult.duration.toFixed(0)}ms
              </span>
            )}
          </div>
          <div style={{ flex: 1, overflow: 'auto', padding: '12px' }}>
            {!activeResult ? (
              <div style={{ color: COLORS.dim, fontSize: '13px', textAlign: 'center', paddingTop: '40px' }}>
                Execute a query to see results
              </div>
            ) : activeResult.result.ok && activeResult.result.columns.length > 0 ? (
              <ResultTable columns={activeResult.result.columns} rows={activeResult.result.rows} message={activeResult.result.message} />
            ) : (
              <div style={{
                padding: '12px 16px', borderRadius: '6px', fontSize: '13px',
                fontFamily: 'JetBrains Mono',
                background: activeResult.result.ok ? `${COLORS.green}1A` : `${COLORS.red}1A`,
                border: `1px solid ${activeResult.result.ok ? COLORS.green : COLORS.red}`,
                color: activeResult.result.ok ? COLORS.green : COLORS.red,
              }}>
                {activeResult.result.ok ? '✓ ' : '✗ ERROR: '}{activeResult.result.message}
              </div>
            )}
          </div>
        </div>
      </div>

      {/* Right: Sample Queries + History */}
      <div style={{ width: '300px', display: 'flex', flexDirection: 'column', gap: '12px' }}>
        {/* Sample Queries */}
        <div style={{
          background: COLORS.surface, borderRadius: '10px', border: `1px solid ${COLORS.border}`,
          overflow: 'hidden', flexShrink: 0,
        }}>
          <div style={{
            padding: '8px 14px', borderBottom: `1px solid ${COLORS.border}`,
            fontSize: '11px', color: COLORS.dim, textTransform: 'uppercase', letterSpacing: '1px',
          }}>
            Quick Queries
          </div>
          <div style={{ padding: '6px', maxHeight: '220px', overflow: 'auto' }}>
            {SAMPLE_QUERIES.map((q, i) => (
              <button key={i} onClick={() => { setSql(q); }}
                onDoubleClick={() => executeSQL(q)}
                title="Click to load, double-click to execute"
                style={{
                  display: 'block', width: '100%', textAlign: 'left',
                  padding: '6px 10px', marginBottom: '2px', borderRadius: '4px',
                  border: 'none', background: 'transparent', cursor: 'pointer',
                  color: COLORS.text, fontFamily: 'JetBrains Mono', fontSize: '10px',
                  lineHeight: '1.4', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis',
                }}
                onMouseEnter={e => { e.currentTarget.style.background = '#ffffff08'; }}
                onMouseLeave={e => { e.currentTarget.style.background = 'transparent'; }}
              >
                {q.startsWith('SELECT') || q.startsWith('SHOW') || q.startsWith('DESCRIBE')
                  ? <span style={{ color: COLORS.cyan }}>{q}</span>
                  : q.startsWith('INSERT')
                  ? <span style={{ color: COLORS.green }}>{q}</span>
                  : q.startsWith('CREATE') || q.startsWith('DROP')
                  ? <span style={{ color: COLORS.yellow }}>{q}</span>
                  : <span style={{ color: COLORS.orange }}>{q}</span>
                }
              </button>
            ))}
          </div>
        </div>

        {/* History */}
        <div style={{
          flex: 1, background: COLORS.surface, borderRadius: '10px',
          border: `1px solid ${COLORS.border}`, overflow: 'hidden', display: 'flex', flexDirection: 'column',
        }}>
          <div style={{
            padding: '8px 14px', borderBottom: `1px solid ${COLORS.border}`,
            fontSize: '11px', color: COLORS.dim, display: 'flex', justifyContent: 'space-between',
            textTransform: 'uppercase', letterSpacing: '1px', flexShrink: 0,
          }}>
            <span>History</span>
            {history.length > 0 && (
              <button onClick={() => setHistory([])} style={{
                border: 'none', background: 'none', color: COLORS.dim, fontSize: '10px',
                cursor: 'pointer', textTransform: 'uppercase',
              }}>Clear</button>
            )}
          </div>
          <div style={{ flex: 1, overflow: 'auto', padding: '6px' }}>
            <AnimatePresence>
              {history.map((entry, i) => (
                <motion.button
                  key={entry.timestamp.getTime()}
                  initial={{ opacity: 0, y: -10 }}
                  animate={{ opacity: 1, y: 0 }}
                  onClick={() => { setSql(entry.sql); setActiveResult(entry); }}
                  style={{
                    display: 'block', width: '100%', textAlign: 'left',
                    padding: '8px 10px', marginBottom: '4px', borderRadius: '6px',
                    border: `1px solid ${activeResult === entry ? COLORS.border : 'transparent'}`,
                    background: activeResult === entry ? COLORS.surface2 : 'transparent',
                    cursor: 'pointer', fontFamily: 'JetBrains Mono', fontSize: '10px',
                    color: COLORS.text,
                  }}
                >
                  <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '3px' }}>
                    <span style={{ color: entry.result.ok ? COLORS.green : COLORS.red }}>
                      {entry.result.ok ? '✓' : '✗'}
                    </span>
                    <span style={{ color: COLORS.dim, fontSize: '9px' }}>
                      {entry.duration.toFixed(0)}ms
                    </span>
                  </div>
                  <div style={{
                    whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis',
                    color: COLORS.dim,
                  }}>
                    {entry.sql}
                  </div>
                </motion.button>
              ))}
            </AnimatePresence>
            {history.length === 0 && (
              <div style={{ color: COLORS.dim, fontSize: '12px', textAlign: 'center', paddingTop: '20px' }}>
                No queries yet
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}

function ResultTable({ columns, rows, message }: { columns: string[]; rows: string[][]; message: string }) {
  return (
    <div>
      <div style={{ overflow: 'auto', borderRadius: '6px', border: `1px solid ${COLORS.border}` }}>
        <table style={{ width: '100%', borderCollapse: 'collapse', fontFamily: 'JetBrains Mono', fontSize: '12px' }}>
          <thead>
            <tr>
              {columns.map((col, i) => (
                <th key={i} style={{
                  padding: '8px 12px', textAlign: 'left',
                  background: COLORS.surface2, color: COLORS.dim,
                  borderBottom: `1px solid ${COLORS.border}`,
                  fontWeight: 600, fontSize: '11px', textTransform: 'uppercase',
                  letterSpacing: '0.5px', whiteSpace: 'nowrap',
                }}>
                  {col}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {rows.map((row, i) => (
              <motion.tr
                key={i}
                initial={{ opacity: 0 }}
                animate={{ opacity: 1 }}
                transition={{ delay: i * 0.03 }}
              >
                {row.map((cell, j) => (
                  <td key={j} style={{
                    padding: '6px 12px',
                    borderBottom: `1px solid ${COLORS.border}`,
                    color: cell === 'NULL' ? COLORS.dim : cell === 'TRUE' ? COLORS.green : cell === 'FALSE' ? COLORS.red : COLORS.text,
                    whiteSpace: 'nowrap',
                    fontStyle: cell === 'NULL' ? 'italic' : 'normal',
                  }}>
                    {cell}
                  </td>
                ))}
              </motion.tr>
            ))}
          </tbody>
        </table>
      </div>
      <div style={{ marginTop: '8px', fontSize: '11px', color: COLORS.dim }}>
        {message}
      </div>
    </div>
  );
}
