'use client';
import { useEffect, useState } from 'react';
type Arm = {
  mode: number;
  textBytes: number;
  dataBytes: number;
  bssBytes: number;
  remainingBeforeOsAllocationsBytes: number;
  minimumFreeHeapBytes: number | null;
  conservativeAppStackSumBytes: number;
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
      {row.remainingBeforeOsAllocationsBytes} B。エミュレーター最小空きヒープ{' '}
      {row.minimumFreeHeapBytes === null
        ? '未測定'
        : `${row.minimumFreeHeapBytes} B`}
      。アプリのスタック上限見積もり {row.conservativeAppStackSumBytes}{' '}
      B（OS・ライブラリ分は別）。実機の時間・ヒープは未測定です。
    </p>
  ) : (
    <p>Pebbleビルド診断を読み込めません。実機の時間・ヒープは未測定です。</p>
  );
}
