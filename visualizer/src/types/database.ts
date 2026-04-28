export interface KVEntry {
  key: string;
  value: string | null;
  sequenceNumber: number;
  isTombstone: boolean;
}

export interface SSTableMeta {
  id: string;
  entryCount: number;
  minKey: string;
  maxKey: string;
  entries: KVEntry[];
}

export interface ConflictEvent {
  key: string;
  winnerSeq: number;
  loserSeq: number;
  isTombstoneResolution: boolean;
}

export interface CompactionMetrics {
  keysProcessed: number;
  duplicatesOverwritten: number;
  tombstonesPurged: number;
  inputBytes: number;
  outputBytes: number;
}

export type CompactionPhase = 'idle' | 'reading' | 'merging' | 'done';

export interface CompactionState {
  phase: CompactionPhase;
  sourceTables: SSTableMeta[];
  activePointers: number[];
  currentCompareKeys: { key: string; sourceIdx: number }[];
  resolvedEntries: KVEntry[];
  conflicts: ConflictEvent[];
  metrics: CompactionMetrics;
  speed: number;
}

export interface WALRecord {
  id: number;
  crc32Stored: string;
  crc32Computed: string;
  opType: 'PUT' | 'DELETE';
  key: string;
  value: string;
  rawHex: string[];
  isCorrupted: boolean;
  corruptedByteIndex: number | null;
  validationResult: 'pending' | 'pass' | 'fail';
}

export interface SSTableFooter {
  fileName: string;
  magic: string;
  isValid: boolean;
  entryCount: number;
}

export type RecoveryPhase = 'active' | 'crashed' | 'recovering' | 'recovered';

export interface RecoveryState {
  phase: RecoveryPhase;
  memtable: { key: string; value: string }[];
  walRecords: WALRecord[];
  scannerPosition: number;
  sstableFooters: SSTableFooter[];
  commandHistory: string[];
}
