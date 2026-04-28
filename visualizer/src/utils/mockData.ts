import type { SSTableMeta, KVEntry } from '../types/database';

// Generate 4 mock SSTables with overlapping keys, tombstones, and conflicts
export function generateMockSSTables(): SSTableMeta[] {
  return [
    {
      id: 'sst_000003',
      entryCount: 5,
      minKey: 'app:config',
      maxKey: 'user:charlie',
      entries: [
        { key: 'app:config', value: 'v3_dark_mode', sequenceNumber: 3, isTombstone: false },
        { key: 'user:alice', value: '{"age":31,"role":"admin"}', sequenceNumber: 3, isTombstone: false },
        { key: 'user:bob', value: null, sequenceNumber: 3, isTombstone: true },
        { key: 'user:charlie', value: '{"age":28,"role":"dev"}', sequenceNumber: 3, isTombstone: false },
      ],
    },
    {
      id: 'sst_000002',
      entryCount: 4,
      minKey: 'app:config',
      maxKey: 'user:eve',
      entries: [
        { key: 'app:config', value: 'v2_light_mode', sequenceNumber: 2, isTombstone: false },
        { key: 'user:alice', value: '{"age":30,"role":"user"}', sequenceNumber: 2, isTombstone: false },
        { key: 'user:dave', value: '{"age":35,"role":"pm"}', sequenceNumber: 2, isTombstone: false },
        { key: 'user:eve', value: '{"age":22,"role":"intern"}', sequenceNumber: 2, isTombstone: false },
      ],
    },
    {
      id: 'sst_000001',
      entryCount: 4,
      minKey: 'app:theme',
      maxKey: 'user:frank',
      entries: [
        { key: 'app:theme', value: 'blue', sequenceNumber: 1, isTombstone: false },
        { key: 'user:bob', value: '{"age":25,"role":"dev"}', sequenceNumber: 1, isTombstone: false },
        { key: 'user:dave', value: null, sequenceNumber: 1, isTombstone: true },
        { key: 'user:frank', value: '{"age":40,"role":"cto"}', sequenceNumber: 1, isTombstone: false },
      ],
    },
    {
      id: 'sst_000000',
      entryCount: 3,
      minKey: 'app:config',
      maxKey: 'user:bob',
      entries: [
        { key: 'app:config', value: 'v1_default', sequenceNumber: 0, isTombstone: false },
        { key: 'user:alice', value: '{"age":29,"role":"user"}', sequenceNumber: 0, isTombstone: false },
        { key: 'user:bob', value: '{"age":24,"role":"jr_dev"}', sequenceNumber: 0, isTombstone: false },
      ],
    },
  ];
}

// Run the mock compaction merge, producing step-by-step events
export interface CompactionStep {
  type: 'compare' | 'keep' | 'overwrite' | 'tombstone_purge' | 'done';
  key: string;
  winner?: KVEntry;
  losers?: KVEntry[];
  sourceIndices?: number[];
}

export function runMockCompaction(tables: SSTableMeta[]): CompactionStep[] {
  const steps: CompactionStep[] = [];
  // Collect all entries grouped by key, newest first (highest seq)
  const keyMap = new Map<string, { entry: KVEntry; sourceIdx: number }[]>();

  tables.forEach((table, idx) => {
    for (const entry of table.entries) {
      if (!keyMap.has(entry.key)) keyMap.set(entry.key, []);
      keyMap.get(entry.key)!.push({ entry, sourceIdx: idx });
    }
  });

  const sortedKeys = Array.from(keyMap.keys()).sort();

  for (const key of sortedKeys) {
    const versions = keyMap.get(key)!;
    versions.sort((a, b) => b.entry.sequenceNumber - a.entry.sequenceNumber);

    steps.push({
      type: 'compare',
      key,
      sourceIndices: versions.map(v => v.sourceIdx),
    });

    const winner = versions[0].entry;
    const losers = versions.slice(1).map(v => v.entry);

    if (losers.length > 0) {
      if (winner.isTombstone) {
        steps.push({ type: 'tombstone_purge', key, winner, losers });
      } else {
        steps.push({ type: 'overwrite', key, winner, losers });
      }
    } else if (winner.isTombstone) {
      steps.push({ type: 'tombstone_purge', key, winner, losers: [] });
    } else {
      steps.push({ type: 'keep', key, winner });
    }
  }

  steps.push({ type: 'done', key: '' });
  return steps;
}
