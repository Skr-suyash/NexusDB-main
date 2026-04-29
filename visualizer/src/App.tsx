import { useState } from 'react'
import CompactionView from './components/compaction/CompactionView'
import RecoveryView from './components/recovery/RecoveryView'
import SQLWorkbench from './components/workbench/SQLWorkbench'

type Tab = 'compaction' | 'recovery' | 'workbench';

export default function App() {
  const [activeTab, setActiveTab] = useState<Tab>('compaction');

  return (
    <div style={{ minHeight: '100vh', background: '#0a0a0f', display: 'flex', flexDirection: 'column' }}>
      {/* Header */}
      <header style={{
        background: 'linear-gradient(180deg, #12121a 0%, #0a0a0f 100%)',
        borderBottom: '1px solid #2a2a3a',
        padding: '0 24px',
        display: 'flex',
        alignItems: 'center',
        gap: '32px',
        height: '64px',
        flexShrink: 0,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
          <div style={{
            width: '32px', height: '32px', borderRadius: '8px',
            background: 'linear-gradient(135deg, #00f0ff, #00ff88)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            fontWeight: 800, fontSize: '14px', color: '#0a0a0f',
          }}>M</div>
          <span style={{ fontWeight: 700, fontSize: '18px', color: '#e0e0e0', fontFamily: 'Inter, sans-serif' }}>
            MosaicDB <span style={{ color: '#666680', fontWeight: 400, fontSize: '14px' }}>Visualizer</span>
          </span>
        </div>

        <nav style={{ display: 'flex', gap: '4px' }}>
          <TabButton
            active={activeTab === 'compaction'}
            onClick={() => setActiveTab('compaction')}
            icon=""
            label="Compaction Matrix"
          />
          <TabButton
            active={activeTab === 'recovery'}
            onClick={() => setActiveTab('recovery')}
            icon=""
            label="Crash & Recover"
          />
          <TabButton
            active={activeTab === 'workbench'}
            onClick={() => setActiveTab('workbench')}
            icon=""
            label="SQL Workbench"
          />
        </nav>
      </header>

      {/* Main content */}
      <main style={{ flex: 1, overflow: 'hidden' }}>
        {activeTab === 'compaction' ? <CompactionView /> : activeTab === 'recovery' ? <RecoveryView /> : <SQLWorkbench />}
      </main>
    </div>
  );
}

function TabButton({ active, onClick, icon, label }: {
  active: boolean; onClick: () => void; icon: string; label: string;
}) {
  return (
    <button
      onClick={onClick}
      style={{
        padding: '8px 20px',
        borderRadius: '8px',
        border: active ? '1px solid #00f0ff33' : '1px solid transparent',
        background: active ? '#00f0ff11' : 'transparent',
        color: active ? '#00f0ff' : '#666680',
        fontSize: '14px',
        fontWeight: 600,
        cursor: 'pointer',
        transition: 'all 0.2s',
        fontFamily: 'Inter, sans-serif',
      }}
      onMouseEnter={(e) => {
        if (!active) e.currentTarget.style.background = '#ffffff08';
      }}
      onMouseLeave={(e) => {
        if (!active) e.currentTarget.style.background = 'transparent';
      }}
    >
      {icon} {label}
    </button>
  );
}
