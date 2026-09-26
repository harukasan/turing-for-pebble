'use client';
import { useEffect, useRef, useState } from 'react';
import { flushSync } from 'react-dom';
import Link from 'next/link';
import { Slider } from '@/components/ui/slider';
import { Switch } from '@/components/ui/switch';
import { RadioGroup, RadioGroupItem } from '@/components/ui/radio-group';
import { presets, type Parameters } from '@/lib/simulation';
import { FloatSimulation } from '@/lib/float-simulation';
import {
  WasmSimulation,
  loadCore,
  effective,
  buildMask,
  type CoreAPI,
} from '@/lib/wasm-simulation';
import {
  drawClock,
  loadFonts,
  fontIndex,
  type ClockFont,
} from '@/lib/clock-fonts';
import { PebbleMemory } from '@/lib/pebble-memory';

const defaults: Parameters = {
  feed: 0.029,
  kill: 0.057,
  da: 1,
  db: 0.5,
  dt: 1,
};
type Engine =
  | 'u8-200'
  | 'q15-100'
  | 'q15-120'
  | 'float-200'
  | 'float-100'
  | 'float-120';
// Startup steps of the watchface, mirrored by device behavior mode.
// Mode 3 (120 x 136) runs 1,250 steps, the older 100 x 114 build 1,800.
const deviceStartupSteps = (engine: Engine) =>
  engine.endsWith('120') ? 1250 : 1800;
// Pending steps after a minute change, as RD_MINUTE_STEPS of the watch:
// with digit avoidance at least 300 steps refill the strokes freed by the
// previous digits, otherwise 16 steps are added.
const DEVICE_MINUTE_STEPS = 300;
const DEVICE_MINUTE_STEPS_PLAIN = 16;
// Halo around the digits held at the equilibrium, in display pixels.
const HALO = 1;
type ActiveSimulation = WasmSimulation | FloatSimulation;
const engineWidth = (engine: Engine) => Number(engine.split('-')[1]);
const gridHeight = (width: number) => Math.floor((width * 228) / 200);
// Core mode of a Wasm engine, and of the watch build it corresponds to.
const engineMode = (engine: Engine) =>
  engine.endsWith('200') ? 0 : engine.endsWith('120') ? 3 : 1;
const isFloat = (engine: Engine) => engine.startsWith('float');
function Range({
  title,
  value,
  min,
  max,
  step,
  onChange,
}: {
  title: string;
  value: number;
  min: number;
  max: number;
  step: number;
  onChange: (v: number) => void;
}) {
  const id = title.split(' ')[0];
  return (
    <div className="control">
      <div className="control-top">
        <label id={`${id}-label`} htmlFor={id}>
          {title}
        </label>
        <input
          id={id}
          type="number"
          min={min}
          max={max}
          step={step}
          value={value}
          onChange={(e) => {
            const v = e.target.valueAsNumber;
            if (Number.isFinite(v) && v >= min && v <= max) onChange(v);
          }}
        />
      </div>
      <Slider
        aria-labelledby={`${id}-label`}
        value={[value]}
        min={min}
        max={max}
        step={step}
        onValueChange={(v) => onChange(Array.isArray(v) ? v[0] : v)}
      />
    </div>
  );
}
function Choices({
  label,
  value,
  options,
  onChange,
}: {
  label: string;
  value: string;
  options: [string, string][];
  onChange: (s: string) => void;
}) {
  return (
    <RadioGroup
      aria-label={label}
      className="choices"
      value={value}
      onValueChange={(v) => onChange(String(v))}
    >
      {options.map(([key, text]) => (
        <label key={key}>
          <RadioGroupItem value={key} />
          {text}
        </label>
      ))}
    </RadioGroup>
  );
}
export default function Home() {
  const canvas = useRef<HTMLCanvasElement>(null),
    sim = useRef<ActiveSimulation | null>(null);
  const [params, setParams] = useState(defaults);
  const [engine, setEngine] = useState<Engine>('q15-120'),
    [seed, setSeed] = useState(42),
    [revision, setRevision] = useState(0);
  const [running, setRunning] = useState(false),
    [speed, setSpeed] = useState(16),
    [clock, setClock] = useState(true),
    [avoid, setAvoid] = useState(true),
    [font, setFont] = useState<ClockFont>('leco');
  const [quantize, setQuantize] = useState(true),
    [interpolate, setInterpolate] = useState(true),
    [palette, setPalette] = useState('green'),
    [actual, setActual] = useState(false);
  const [stats, setStats] = useState({ steps: 0, ms: 0 }),
    [notice, setNotice] = useState(''),
    [advance, setAdvance] = useState(0);
  const [deviceMode, setDeviceMode] = useState(false),
    [memory, setMemory] = useState<number[]>([]);
  const width = engineWidth(engine);
  const floatMode = isFloat(engine);
  const lightUntil = useRef(0);
  const config = useRef({
    params,
    running,
    speed,
    clock,
    avoid,
    font,
    quantize,
    interpolate,
    palette,
    deviceMode,
  });
  useEffect(() => {
    config.current = {
      params,
      running,
      speed,
      clock,
      avoid,
      font,
      quantize,
      interpolate,
      palette,
      deviceMode,
    };
  }, [
    params,
    running,
    speed,
    clock,
    avoid,
    font,
    quantize,
    interpolate,
    palette,
    deviceMode,
  ]);
  useEffect(() => {
    type Tool = {
      name: string;
      description: string;
      inputSchema: object;
      annotations: { readOnlyHint: boolean };
      execute: (input: unknown) => unknown;
    };
    const context = (
      document as Document & {
        modelContext?: {
          registerTool: (
            tool: Tool,
            options: { signal: AbortSignal },
          ) => void | Promise<void>;
        };
      }
    ).modelContext;
    if (!context?.registerTool) return;
    const lifecycle = new AbortController();
    const tool: Tool = {
      name: 'configure_turing_pattern',
      description:
        'Set Feed and Kill in the visible simulation. Optionally reset to the current seed.',
      inputSchema: {
        type: 'object',
        properties: {
          feed: { type: 'number', minimum: 0.01, maximum: 0.1 },
          kill: { type: 'number', minimum: 0.03, maximum: 0.075 },
          reset: { type: 'boolean' },
        },
        required: ['feed', 'kill'],
        additionalProperties: false,
      },
      annotations: { readOnlyHint: false },
      execute(input) {
        if (!input || typeof input !== 'object')
          throw new Error('Expected an object');
        const v = input as Record<string, unknown>;
        if (
          Object.keys(v).some((k) => !['feed', 'kill', 'reset'].includes(k)) ||
          typeof v.feed !== 'number' ||
          !Number.isFinite(v.feed) ||
          v.feed < 0.01 ||
          v.feed > 0.1 ||
          typeof v.kill !== 'number' ||
          !Number.isFinite(v.kill) ||
          v.kill < 0.03 ||
          v.kill > 0.075 ||
          (v.reset !== undefined && typeof v.reset !== 'boolean')
        )
          throw new Error('Invalid parameters');
        const feed = v.feed,
          kill = v.kill;
        flushSync(() => {
          setParams((p) => ({ ...p, feed, kill }));
          if (v.reset) setRevision((n) => n + 1);
        });
        return { feed, kill, reset: v.reset === true };
      },
    };
    try {
      Promise.resolve(
        context.registerTool(tool, { signal: lifecycle.signal }),
      ).catch(() => {});
    } catch {
      /* Optional browser capability. */
    }
    return () => lifecycle.abort();
  }, []);
  const pending = useRef(0),
    manualPending = useRef(0),
    core = useRef<CoreAPI | null>(null);
  useEffect(() => {
    if (advance) manualPending.current += 100;
  }, [advance]);
  useEffect(() => {
    let cancelled = false;
    sim.current?.dispose();
    sim.current = null;
    // The Float32 engines need the core too, for the clock mask.
    Promise.all([loadCore(), loadFonts()])
      .then(([api]) => {
        if (cancelled) return;
        core.current = api;
        const next = isFloat(engine)
          ? new FloatSimulation(engineWidth(engine), seed)
          : new WasmSimulation(api, engineMode(engine), seed);
        sim.current = next;
        setStats({ steps: 0, ms: 0 });
        if (next instanceof WasmSimulation)
          setMemory([...next.components, next.bytes, next.linearBytes]);
        else setMemory([]);
      })
      .catch((e) => {
        if (!cancelled) {
          setNotice(`読み込み失敗: ${String(e)}`);
          setRunning(false);
        }
      });
    pending.current = config.current.deviceMode
      ? deviceStartupSteps(engine)
      : 0;
    manualPending.current = 0;
    lightUntil.current = 0;
    return () => {
      cancelled = true;
      sim.current?.dispose();
      sim.current = null;
    };
  }, [engine, seed, revision]);
  useEffect(() => {
    const startFrame = requestAnimationFrame(() =>
      setRunning(
        !window.matchMedia('(prefers-reduced-motion: reduce)').matches,
      ),
    );
    const ctx = canvas.current?.getContext('2d');
    if (!ctx) {
      setNotice('Canvas 2Dを利用できません。別のブラウザで開いてください。');
      return;
    }
    const offscreen = document.createElement('canvas'),
      off = offscreen.getContext('2d')!;
    let frame = 0,
      lastPaint = 0,
      lastStats = 0,
      measured = 0;
    let pixels: ImageData;
    let lastMinute = Math.floor(Date.now() / 60000),
      focused = document.hasFocus();
    // The simulation and key (font and minute) the mask was built for.
    let maskSim: ActiveSimulation | null = null,
      maskKey = '';
    const stopLight = () => {
      lightUntil.current = 0;
    };
    const blur = () => {
      focused = false;
      stopLight();
    };
    const focus = () => {
      focused = true;
    };
    window.addEventListener('blur', blur);
    window.addEventListener('focus', focus);
    document.addEventListener('visibilitychange', stopLight);
    const render = (now: number) => {
      frame = requestAnimationFrame(render);
      const c = config.current;
      // Like the watch, pending startup or minute work runs in consecutive
      // 30 ms slices with the screen painted about every 50 ms.
      const deviceWork = c.deviceMode && c.running && pending.current > 0;
      if (document.hidden || (c.deviceMode && !focused)) return;
      if (!deviceWork && now - lastPaint < (c.deviceMode ? 100 : 1000 / 30))
        return;
      // Pending work paints every 50 ms and animation every 100 ms, like the watch.
      const s = sim.current;
      if (!s) return;
      const start = performance.now();
      let steps = 0;
      const minute = Math.floor(Date.now() / 60000);
      const avoiding = c.clock && c.avoid;
      if (minute !== lastMinute) {
        if (c.deviceMode && c.running)
          pending.current = avoiding
            ? Math.max(pending.current, DEVICE_MINUTE_STEPS)
            : pending.current + DEVICE_MINUTE_STEPS_PLAIN;
        lastMinute = minute;
      }
      // Keep the pattern out of the digits: rebuild the mask when the
      // simulation, the font, or the minute changes.
      const key = avoiding ? `${c.font}|${minute}` : '';
      if ((s !== maskSim || key !== maskKey) && core.current) {
        s.setMask(
          key
            ? buildMask(
                core.current,
                s.width,
                s.height,
                fontIndex(c.font),
                new Date(),
                HALO,
              )
            : null,
        );
        maskSim = s;
        maskKey = key;
      }
      const manual = manualPending.current > 0;
      const budget = deviceWork && !manual ? 30 : 8;
      const target = manual
        ? Math.min(manualPending.current, 8)
        : c.running
          ? pending.current > 0
            ? c.deviceMode
              ? pending.current
              : Math.min(pending.current, 8)
            : c.deviceMode
              ? now < lightUntil.current
                ? 8
                : 0
              : c.speed
          : 0;
      while (steps < target) {
        s.step(c.params);
        steps++;
        if (performance.now() - start >= budget) break;
      }
      if (manual) manualPending.current -= steps;
      else if (pending.current > 0) pending.current -= steps;
      if (steps) measured = (performance.now() - start) / steps;
      if (deviceWork && pending.current > 0 && now - lastPaint < 50) return;
      lastPaint = now;
      if (!pixels) {
        offscreen.width = 200;
        offscreen.height = 228;
        pixels = off.createImageData(200, 228);
      }
      s.render(pixels, c.palette, c.quantize, c.interpolate);
      off.putImageData(pixels, 0, 0);
      ctx.imageSmoothingEnabled = false;
      ctx.drawImage(offscreen, 0, 0);
      if (c.clock) drawClock(ctx, new Date(), c.font);
      if (now - lastStats > 400) {
        setStats({ steps: s.steps, ms: measured });
        lastStats = now;
      }
    };
    frame = requestAnimationFrame(render);
    return () => {
      cancelAnimationFrame(frame);
      cancelAnimationFrame(startFrame);
      window.removeEventListener('blur', blur);
      window.removeEventListener('focus', focus);
      document.removeEventListener('visibilitychange', stopLight);
    };
  }, []);
  const settings = {
    model: 'Gray-Scott',
    version: 3,
    method: engine,
    rounding: floatMode
      ? 'float32-storage'
      : engine === 'u8-200'
        ? 'floyd-steinberg-dithered'
        : 'floyd-steinberg-nearest',
    storage: floatMode
      ? 'float32'
      : engine === 'u8-200'
        ? 'packed-a7-linear-b9-sqrt'
        : 'q15',
    effective: floatMode ? params : effective(params, engineMode(engine)),
    ...params,
    width,
    height: gridHeight(width),
    seed,
    boundary: 'periodic',
    laplacian: { center: -1, axial: 0.2, diagonal: 0.05 },
    initialization: '24 display-coordinate disks, radius 4–9, A=0.5 B=0.25',
    palette,
    quantize,
    interpolate,
    clock,
    font,
    avoidDigits: clock && avoid,
    halo: HALO,
    minuteSteps:
      clock && avoid ? DEVICE_MINUTE_STEPS : DEVICE_MINUTE_STEPS_PLAIN,
    steps: stats.steps,
  };
  const reset = () => {
    setRevision((v) => v + 1);
    setNotice('同じシードで初期化しました。');
  };
  function save(kind: 'json' | 'png' | 'h') {
    if (kind === 'h' && floatMode) return;
    const a = document.createElement('a');
    a.download = `turing-${seed}-${stats.steps}.${kind}`;
    if (kind === 'h') {
      const q = effective(params);
      const text = `/* Generated configuration, core v3 */\n#ifndef RD_MODE\n#define RD_MODE ${engineMode(engine)}\n#endif\n#define RD_SEED ${seed}u\n#define RD_FEED ${Math.round(Number(q.feed) * 32768)}\n#define RD_KILL ${Math.round(Number(q.kill) * 32768)}\n#define RD_DA ${Math.round(Number(q.da) * 32768)}\n#define RD_DB ${Math.round(Number(q.db) * 32768)}\n#define RD_DT ${Math.round(Number(q.dt) * 32768)}\n#define RD_PALETTE ${palette === 'green' ? 0 : palette === 'blue' ? 1 : 2}\n#define RD_CLOCK ${Number(clock)}\n#ifndef RD_STARTUP_STEPS\n#define RD_STARTUP_STEPS ${deviceStartupSteps(engine)}\n#endif\n#ifndef RD_RENDER_FLAGS\n#define RD_RENDER_FLAGS ${interpolate ? 2 : 0}\n#endif\n#ifndef RD_FONT\n#define RD_FONT ${fontIndex(font)}\n#endif\n#ifndef RD_AVOID\n#define RD_AVOID ${Number(avoid)}\n#endif\n#define RD_HALO ${HALO}\n#define RD_MINUTE_STEPS (RD_AVOID ? ${DEVICE_MINUTE_STEPS} : ${DEVICE_MINUTE_STEPS_PLAIN})\n`;
      const url = URL.createObjectURL(new Blob([text]));
      a.href = url;
      a.download = 'config.h';
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    } else if (kind === 'png') {
      a.href = canvas.current!.toDataURL('image/png');
      a.click();
    } else {
      const url = URL.createObjectURL(
        new Blob(
          [
            JSON.stringify(
              { ...settings, steps: sim.current?.steps ?? 0 },
              null,
              2,
            ),
          ],
          { type: 'application/json' },
        ),
      );
      a.href = url;
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
    setNotice(
      kind === 'png'
        ? '200 × 228のPNGを書き出しました。'
        : kind === 'h'
          ? 'Pebble用config.hを書き出しました。'
          : '設定JSONを書き出しました。',
    );
  }
  return (
    <main>
      <header>
        <h1>
          Turing Lab <span>/ Pebble Time 2</span>
        </h1>
        <div className="eyebrow">
          REACTION–DIFFUSION
          <br />
          WATCHFACE STUDY / 01
        </div>
      </header>
      <div className="workspace">
        <div>
          <div className={`stage ${actual ? 'actual' : ''}`}>
            <div className="stage-top">
              <span>DISPLAY / 200 × 228</span>
              <span>{running ? '● RUNNING' : 'Ⅱ PAUSED'}</span>
            </div>
            <canvas
              ref={canvas}
              width={200}
              height={228}
              aria-label="反応拡散パターンと現在時刻のプレビュー。クリックまたはドラッグで反応の種を追加"
              onPointerDown={(e) => {
                e.currentTarget.setPointerCapture(e.pointerId);
                const r = e.currentTarget.getBoundingClientRect();
                sim.current?.seedAt(
                  Math.max(
                    0,
                    Math.min(
                      width - 1,
                      Math.floor(((e.clientX - r.left) / r.width) * width),
                    ),
                  ),
                  Math.max(
                    0,
                    Math.min(
                      gridHeight(width) - 1,
                      Math.floor(
                        ((e.clientY - r.top) / r.height) * gridHeight(width),
                      ),
                    ),
                  ),
                );
              }}
              onPointerMove={(e) => {
                if (e.buttons !== 1) return;
                const r = e.currentTarget.getBoundingClientRect();
                if (
                  e.clientX < r.left ||
                  e.clientX >= r.right ||
                  e.clientY < r.top ||
                  e.clientY >= r.bottom
                )
                  return;
                sim.current?.seedAt(
                  Math.floor(((e.clientX - r.left) / r.width) * width),
                  Math.floor(
                    ((e.clientY - r.top) / r.height) * gridHeight(width),
                  ),
                );
              }}
            />
            <div>
              <div className="toolbar">
                <button
                  className="action primary"
                  onClick={() => setRunning((v) => !v)}
                >
                  {running ? '一時停止' : '再生'}
                </button>
                <button
                  className="action"
                  disabled={running}
                  onClick={() => setAdvance((v) => v + 1)}
                >
                  100ステップ進める
                </button>
                <button className="action" onClick={reset}>
                  初期化
                </button>
              </div>
              <p className="hint">
                模様が育つまで数千ステップ。画面をなぞって種を追加。
              </p>
              <button
                className="action"
                style={{ marginTop: 10 }}
                onClick={() => {
                  sim.current?.seedAt(
                    Math.floor(width / 2),
                    Math.floor(gridHeight(width) / 2),
                  );
                }}
              >
                中央に種を追加
              </button>
            </div>
          </div>
          <div className="stats">
            <div>
              <strong>{stats.steps.toLocaleString()}</strong>
              <span>計算ステップ</span>
            </div>
            <div>
              <strong>{stats.ms.toFixed(2)} ms</strong>
              <span>1ステップ / このブラウザ</span>
            </div>
            <div>
              <strong>
                {floatMode
                  ? ((width * gridHeight(width) * 16) / 1024).toFixed(1)
                  : ((memory[6] ?? 0) / 1024).toFixed(1)}{' '}
                KiB
              </strong>
              <span>{floatMode ? 'Float32の計算配列' : '共通Cの必要量'}</span>
            </div>
          </div>
          <p className="notes">
            パラメータの変更は現在の模様に反映します。方式を変えると同じシードで初期化します。同じ条件で比べるには同じステップ数まで進めてください。
          </p>
          <details open>
            <summary>メモリ内訳</summary>
            <div className="notes">
              {floatMode ? (
                <>
                  <p>
                    Float32参照実装: A・Bの現在と次の配列で{' '}
                    {width * gridHeight(width) * 16}{' '}
                    B。Pebble向けの共通C必要量とヒープ測定値は対象外です。
                  </p>
                  <p>
                    Web固有: ImageData 182400 B / Canvas 2面の画素相当 364800
                    B。ブラウザ全体の使用量ではありません。
                  </p>
                </>
              ) : (
                <>
                  <p>
                    共通C: 濃度場 {memory[0]} B / 行バッファ {memory[1]} B /
                    制御 {memory[2]} B / 描画行 {memory[3]} B /
                    アラインメント余裕 {memory[4]} B / 数字マスク {memory[5]} B
                  </p>
                  <p>
                    Web固有: Wasm linear memory {memory[7]}
                    B（共通Cを内包） / ImageData 182400 B / Canvas 2面の画素相当
                    364800 B。ブラウザ全体の使用量ではありません。
                  </p>
                  <PebbleMemory mode={engineMode(engine)} />
                  <p>
                    最小空きヒープは実測値、OS追加確保前の空きは見積もりです。両者は加算しません。採用判定は実機検証待ちです。
                  </p>
                </>
              )}
              <Link
                href="/reports/comparison.json"
                target="_blank"
                prefetch={false}
              >
                比較・ビルド診断JSON
              </Link>
              <br />
              <Link
                href="/reports/comparison.png"
                target="_blank"
                prefetch={false}
              >
                全プリセットの比較画像（10,000ステップ）
              </Link>
              <br />
              <Link
                href="/reports/thin-lines.png"
                target="_blank"
                prefetch={false}
              >
                細線プリセットの比較画像（10,000ステップ）
              </Link>
            </div>
          </details>
          <details>
            <summary>モデルと実機への移植について</summary>
            <div className="notes">
              <p>
                Gray–Scottモデルを陽的Euler法で計算します。境界は上下・左右で循環。3
                × 3ラプラシアンの重みは中心 −1、上下左右 0.2、対角
                0.05。濃度は0〜1に制限します。
              </p>
              <p>
                低い計算解像度は最近傍で200 ×
                228に拡大します。64色モードでは各RGB成分を0・85・170・255に丸めます。表示倍率1×はCSSピクセル基準で、実寸ではありません。
              </p>
              <p>
                共通Cの必要量はC
                APIの値です。Pebbleのコード・静的領域と実行時最小空きヒープは診断レポートを参照してください。ブラウザの速度は実機性能の推定ではありません。
              </p>
              <a
                href="https://www.karlsims.com/rd.html"
                target="_blank"
                rel="noreferrer"
              >
                Gray–Scottモデルの参考資料 ↗
              </a>
              <br />
              <a
                href="https://developer.rebble.io/guides/tools-and-resources/hardware-information/"
                target="_blank"
                rel="noreferrer"
              >
                Pebbleの画面仕様 ↗
              </a>
            </div>
          </details>
        </div>
        <aside>
          <section className="controls">
            <h2>
              <span className="eyebrow">01 / </span> パターン
            </h2>
            <div className="presets">
              {presets.map((p) => (
                <button
                  className={`action ${params.feed === p.feed && params.kill === p.kill ? 'primary' : ''}`}
                  key={p.name}
                  onClick={() => {
                    setParams({ ...defaults, feed: p.feed, kill: p.kill });
                    setRevision((v) => v + 1);
                    setNotice(`${p.name}の設定で初期化しました。`);
                  }}
                >
                  {p.name}
                </button>
              ))}
            </div>
            <Range
              title="Feed / Aの供給"
              value={params.feed}
              min={0.01}
              max={0.1}
              step={0.0001}
              onChange={(feed) => setParams((p) => ({ ...p, feed }))}
            />
            <Range
              title="Kill / Bの除去"
              value={params.kill}
              min={0.03}
              max={0.075}
              step={0.0001}
              onChange={(kill) => setParams((p) => ({ ...p, kill }))}
            />
            <details style={{ marginTop: 18 }}>
              <summary>拡散係数・時間刻み</summary>
              <Range
                title="Da / Aの拡散"
                value={params.da}
                min={0.1}
                max={1}
                step={0.01}
                onChange={(da) => setParams((p) => ({ ...p, da }))}
              />
              <Range
                title="Db / Bの拡散"
                value={params.db}
                min={0.01}
                max={0.5}
                step={0.01}
                onChange={(db) => setParams((p) => ({ ...p, db }))}
              />
              <Range
                title="dt / 時間刻み"
                value={params.dt}
                min={0.1}
                max={1}
                step={0.1}
                onChange={(dt) => setParams((p) => ({ ...p, dt }))}
              />
            </details>
          </section>
          <section className="controls">
            <h2>
              <span className="eyebrow">02 / </span> 計算
            </h2>
            <Choices
              label="計算実装"
              value={engine}
              options={[
                ['u8-200', 'Wasm A7+B9 / 200 × 228'],
                ['float-200', 'Float32 / 200 × 228'],
                ['q15-120', 'Wasm Q15 / 120 × 136'],
                ['float-120', 'Float32 / 120 × 136'],
                ['q15-100', 'Wasm Q15 / 100 × 114'],
                ['float-100', 'Float32 / 100 × 114'],
              ]}
              onChange={(value) => setEngine(value as Engine)}
            />
            <p className="hint">
              同じ解像度の組で保存精度を比較できます。方式を切り替えると初期化します。
            </p>
            <label className="toggle" htmlFor="device-mode">
              実機動作モード
              <Switch
                id="device-mode"
                checked={deviceMode}
                onCheckedChange={(v) => {
                  setDeviceMode(v);
                  setRevision((n) => n + 1);
                }}
              />
            </label>
            <button
              className="action"
              onClick={() => {
                lightUntil.current = performance.now() + 5000;
              }}
            >
              点灯を模擬（5秒）
            </button>
            <button
              className="action"
              onClick={() => {
                lightUntil.current = 0;
              }}
            >
              消灯
            </button>
            <Range
              title="Speed / ステップ数・描画"
              value={speed}
              min={1}
              max={64}
              step={1}
              onChange={setSpeed}
            />
            <p className="hint">
              通常は最大30描画/秒、計算予算8ms。実機動作モードは分が変わるたびに300ステップ（数字を避けないときは16ステップ）、点灯中は最大5秒・10fps・8ステップ/描画。
            </p>
            <div className="control-top" style={{ marginTop: 18 }}>
              <label htmlFor="seed">乱数シード</label>
              <input
                id="seed"
                type="number"
                min="0"
                max="4294967295"
                step="1"
                value={seed}
                onChange={(e) => {
                  const n = e.target.valueAsNumber;
                  if (Number.isInteger(n) && n >= 0 && n <= 4294967295)
                    setSeed(n);
                }}
              />
            </div>
          </section>
          <section className="controls">
            <h2>
              <span className="eyebrow">03 / </span> 文字盤の見え方
            </h2>
            <Choices
              label="配色"
              value={palette}
              options={[
                ['green', 'ライム'],
                ['blue', 'シアン'],
                ['mono', '白黒'],
              ]}
              onChange={setPalette}
            />
            <label className="toggle" htmlFor="quantize">
              Pebbleの64色に量子化
              <Switch
                id="quantize"
                checked={quantize}
                onCheckedChange={setQuantize}
              />
            </label>
            <label className="toggle" htmlFor="interpolate">
              計算セルの間を補間
              <Switch
                id="interpolate"
                checked={interpolate}
                onCheckedChange={setInterpolate}
              />
            </label>
            <p className="hint">
              Bの濃度をセルの間で補間してから色にします。120 ×
              136のように画面の整数分の1でない計算でも、縞の輪郭が画素単位で滑らかになります。
            </p>
            <label className="toggle" htmlFor="clock">
              時刻・日付を表示
              <Switch id="clock" checked={clock} onCheckedChange={setClock} />
            </label>
            <Choices
              label="時計フォント"
              value={font}
              options={[
                ['leco', 'LECO'],
                ['bitham', 'Bitham'],
              ]}
              onChange={(value) => setFont(value as ClockFont)}
            />
            <label className="toggle" htmlFor="avoid">
              数字を避ける
              <Switch
                id="avoid"
                checked={avoid}
                disabled={!clock}
                onCheckedChange={setAvoid}
              />
            </label>
            <p className="hint">
              数字の下ではBを0に保ち、周囲5セルで除去率（Kill）を0.075まで上げて、模様を数字の手前で自然に途切れさせます。
            </p>
            <label className="toggle" htmlFor="actual">
              表示倍率 1×
              <Switch
                id="actual"
                checked={actual}
                onCheckedChange={setActual}
              />
            </label>
          </section>
          <div className="toolbar" style={{ justifyContent: 'flex-start' }}>
            <button className="action" onClick={() => save('json')}>
              設定JSONを保存 ↓
            </button>
            <button
              className="action"
              disabled={floatMode}
              title={
                floatMode ? 'Pebble用設定はWasm方式で出力できます' : undefined
              }
              onClick={() => save('h')}
            >
              Pebble config.h ↓
            </button>
            <button className="action" onClick={() => save('png')}>
              PNGを保存 ↓
            </button>
          </div>
          <output className="hint" style={{ minHeight: 20, display: 'block' }}>
            {notice}
          </output>
          <details>
            <summary>現在の設定を見る</summary>
            <pre>{JSON.stringify(settings, null, 2)}</pre>
            <p className="hint">
              JSONは設定の記録です。濃度場や手描きの種は含みません。
            </p>
          </details>
        </aside>
      </div>
      <footer>
        LOCAL EXPERIMENT · GRAY–SCOTT MODEL ·
        パターンの形と時計の読みやすさを試すためのデモ
      </footer>
    </main>
  );
}
