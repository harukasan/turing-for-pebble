import type { Metadata } from 'next';
import './globals.css';
export const metadata: Metadata = { title: 'Turing Lab · Pebble Time 2', description: '反応拡散パターンを調整する、Pebble Time 2文字盤の実験室。' };
export default function RootLayout({ children }: { children: React.ReactNode }) {
  return <html lang="ja"><body>{children}</body></html>;
}
