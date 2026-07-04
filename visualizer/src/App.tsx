import { useState } from 'react'
import CompactionView from './components/compaction/CompactionView'
import RecoveryView from './components/recovery/RecoveryView'
import SQLWorkbench from './components/workbench/SQLWorkbench'

type Tab = 'compaction' | 'recovery' | 'workbench';

export default function App() {
  const [activeTab, setActiveTab] = useState<Tab>('compaction');

  return (
    <div style={{ minHeight: '100vh', background: 'var(--color-bg)', display: 'flex', flexDirection: 'column' }}>
      {/* Header */}
      <header style={{
        background: 'rgba(24, 24, 27, 0.65)',
        backdropFilter: 'blur(12px)',
        borderBottom: '1px solid var(--color-border)',
        padding: '0 24px',
        display: 'flex',
        alignItems: 'center',
        gap: '32px',
        height: '64px',
        flexShrink: 0,
        position: 'sticky',
        top: 0,
        zIndex: 50,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          <div style={{
            width: '32px', height: '32px', borderRadius: '8px',
            background: 'var(--color-text)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            fontWeight: 800, fontSize: '14px', color: 'var(--color-bg)',
          }}>M</div>
          <span style={{ fontWeight: 600, fontSize: '16px', color: 'var(--color-text)', fontFamily: 'var(--font-sans)', letterSpacing: '-0.01em' }}>
            MosaicDB <span style={{ color: 'var(--color-text-dim)', fontWeight: 400 }}>Visualizer</span>
          </span>
        </div>

        <nav style={{ display: 'flex', gap: '8px' }}>
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
        padding: '6px 16px',
        borderRadius: '6px',
        border: 'none',
        background: active ? 'var(--color-surface-2)' : 'transparent',
        color: active ? 'var(--color-text)' : 'var(--color-text-dim)',
        fontSize: '14px',
        fontWeight: 500,
        cursor: 'pointer',
        transition: 'all 0.15s ease',
        fontFamily: 'var(--font-sans)',
      }}
      onMouseEnter={(e) => {
        if (!active) {
          e.currentTarget.style.color = 'var(--color-text)';
          e.currentTarget.style.background = 'rgba(255, 255, 255, 0.05)';
        }
      }}
      onMouseLeave={(e) => {
        if (!active) {
          e.currentTarget.style.color = 'var(--color-text-dim)';
          e.currentTarget.style.background = 'transparent';
        }
      }}
    >
      {icon} {label}
    </button>
  );
}
