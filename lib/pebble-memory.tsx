'use client';
import { useEffect, useState } from 'react';
type Arm = {
  mode: number;
  textBytes: number;
  dataBytes: number;
  bssBytes: number;
  remainingBeforeOsAllocationsBytes: number;
  conservativeAppStackSumBytes: number;
  physicalStartupMs: number | null;
  physicalStepsPerSecond: number | null;
  physicalMinimumFreeHeapBytes: number | null;
};
export function PebbleMemory({ mode }: { mode: number }) {
  const [rows, setRows] = useState<Arm[]>([]);
  useEffect(() => {
    let active = true;
    fetch('/reports/comparison.json')
      .then((r) => {
        if (!r.ok) throw Error('report unavailable');
        return r.json();
      })
      .then((r) => {
        if (active) setRows((r as { arm?: Arm[] }).arm ?? []);
      })
      .catch(() => {});
    return () => {
      active = false;
    };
  }, []);
  const row = rows.find((r) => r.mode === mode);
  return row ? (
    <p>
      Pebble ARM実測: コード {row.textBytes} B / 静的領域{' '}
      {row.dataBytes + row.bssBytes} B。OS追加確保前の空き見積もり{' '}
      {row.remainingBeforeOsAllocationsBytes} B。アプリのスタック上限見積もり{' '}
      {row.conservativeAppStackSumBytes} B（OS・ライブラリ分は別）。
      {row.physicalStartupMs === null
        ? '実機は未測定です。'
        : `実機: 起動 ${(row.physicalStartupMs / 1000).toFixed(1)} 秒、毎秒 ${row.physicalStepsPerSecond} ステップ、最小空きヒープ ${row.physicalMinimumFreeHeapBytes} B。`}
    </p>
  ) : (
    <p>Pebbleビルド診断を読み込めません。実機の時間・ヒープは未測定です。</p>
  );
}
