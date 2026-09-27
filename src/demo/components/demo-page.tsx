/** @jsxImportSource react */
import { useId, type PointerEvent } from "react";
import { PALETTE_IDS, PALETTES } from "../../../lib/palettes";
import { presets, type Parameters } from "../../../lib/simulation";
import { isFloat, type ClockFace } from "../../core";
import { makeSettings } from "../serialization";
import { useDemo } from "../use-demo";
import { Explanation } from "./explanation";
import {
  bounds,
  engines,
  NumberSlider,
  SwitchControl,
  Selector,
  SeedField,
} from "./controls";
import { styles as s } from "../styles";

type MountOptions = { assetBaseUrl?: string };

const pad = (value: number) => String(value).padStart(2, "0");
/** A minute of the day as HH:MM. */
const clockLabel = (minutes: number) =>
  `${pad(Math.floor(minutes / 60))}:${pad(minutes % 60)}`;
/** The current minute of the day. */
const currentMinute = () => {
  const now = new Date();
  return now.getHours() * 60 + now.getMinutes();
};

export function DemoPage({ assetBaseUrl = "/" }: MountOptions) {
  const demo = useDemo(assetBaseUrl);
  const { options, stats, playerRef, canvasRef } = demo;
  const engineGroup = useId();

  function seedAtPointer(event: PointerEvent<HTMLCanvasElement>) {
    const rect = event.currentTarget.getBoundingClientRect();
    playerRef.current?.seed(
      (event.clientX - rect.left) / rect.width,
      (event.clientY - rect.top) / rect.height
    );
  }

  return (
    <main className={s.page}>
      <header>
        <h1 className={s.title}>Turing Pattern Watchface for Pebble</h1>
      </header>
      <div className={s.layout}>
        <div>
          <div className={s.stage}>
            <canvas
              ref={canvasRef}
              className={`${s.canvas} ${options.actual ? s.canvasActual : ""}`}
              width={200}
              height={228}
              tabIndex={0}
              role="button"
              aria-label="反応拡散パターンと現在時刻のプレビュー。ポインターで種を追加。Enter キーまたはスペースキーで中央に種を追加"
              onPointerDown={(event) => {
                event.currentTarget.setPointerCapture(event.pointerId);
                seedAtPointer(event);
              }}
              onPointerMove={(event) => {
                if (event.buttons === 1) seedAtPointer(event);
              }}
              onKeyDown={(event) => {
                if (event.key !== "Enter" && event.key !== " ") return;
                event.preventDefault();
                playerRef.current?.seed(0.5, 0.5);
              }}
            />
            <div className={s.row}>
              <button
                className={`${s.button} ${s.buttonPrimary}`}
                type="button"
                data-action="toggle"
                onClick={() => demo.update("running", !options.running)}
              >
                {options.running ? "一時停止" : "再生"}
              </button>
              <button
                className={s.button}
                type="button"
                data-action="advance"
                disabled={options.running}
                onClick={() => playerRef.current?.advance(100)}
              >
                100ステップ進める
              </button>
              <button
                className={s.button}
                type="button"
                data-action="reset"
                onClick={demo.requestReset}
              >
                初期化
              </button>
            </div>
            <p className={s.note}>
              模様が育つまで数千ステップかかります。プレビューをなぞると種を追加できます。
            </p>
          </div>
          <div className={s.stats} aria-live="off">
            <div className={s.stat}>
              <strong className={s.statValue} data-stat="steps">
                {stats.steps.toLocaleString()}
              </strong>
              <span>計算ステップ</span>
            </div>
            <div className={s.stat}>
              <strong className={s.statValue} data-stat="ms">
                {stats.stepMs.toFixed(2)} ms
              </strong>
              <span>1ステップ / このブラウザ</span>
            </div>
            <div className={s.stat}>
              <strong className={s.statValue} data-stat="memory">
                {stats.bytes === null
                  ? "—"
                  : `${(stats.bytes / 1024).toFixed(1)} KiB`}
              </strong>
              <span data-memory-label>
                {isFloat(options.engine)
                  ? "Float32 の計算配列"
                  : "共通 C の必要量"}
              </span>
            </div>
          </div>
          <Explanation />
        </div>
        <aside className={s.stack}>
          <section className={s.panel} aria-labelledby="pattern-title">
            <h2 className={s.heading} id="pattern-title">
              パターン
            </h2>
            <div className={s.row}>
              {presets.map((preset, index) => (
                <button
                  key={preset.name}
                  className={s.button}
                  type="button"
                  data-preset={index}
                  onClick={() => demo.choosePreset(index)}
                >
                  {preset.name}
                </button>
              ))}
            </div>
            {(Object.keys(bounds) as (keyof Parameters)[]).map((key) => {
              const labels: Record<keyof Parameters, string> = {
                feed: "Feed / A の供給",
                kill: "Kill / B の除去",
                da: "Da / A の拡散",
                db: "Db / B の拡散",
                dt: "dt / 時間刻み",
              };
              const [min, max, step] = bounds[key];
              return (
                <NumberSlider
                  key={key}
                  name={key}
                  label={labels[key]}
                  value={options.params[key]}
                  min={min}
                  max={max}
                  step={step}
                  onChange={(value) => demo.updateParam(key, value)}
                />
              );
            })}
          </section>
          <section className={s.panel} aria-labelledby="compute-title">
            <h2 className={s.heading} id="compute-title">
              計算
            </h2>
            <fieldset className={s.radioGroup}>
              <legend className={s.radioLegend}>計算方式</legend>
              {engines.map(([key, label]) => (
                <label key={key} className={s.radioOption}>
                  <input
                    className={s.radioInput}
                    type="radio"
                    name={engineGroup}
                    data-option="engine"
                    value={key}
                    checked={options.engine === key}
                    onChange={() => demo.update("engine", key)}
                  />
                  <span>{label}</span>
                </label>
              ))}
            </fieldset>
            <NumberSlider
              name="speed"
              label="Speed / 描画ごとのステップ数"
              value={options.speed}
              min={1}
              max={64}
              step={1}
              onChange={(value) => demo.update("speed", value)}
            />
            <SeedField
              value={options.seed}
              onChange={(value) => demo.update("seed", value)}
            />
          </section>
          <section className={s.panel} aria-labelledby="display-title">
            <h2 className={s.heading} id="display-title">
              文字盤の見え方
            </h2>
            <Selector
              name="palette"
              label="配色"
              value={options.palette}
              choices={PALETTES.map((p) => [p.id, p.name])}
              onChange={(value) => {
                const id = PALETTE_IDS.find((p) => p === value);
                if (id) demo.update("palette", id);
              }}
            />
            <SwitchControl
              name="quantize"
              label="Pebble の 64 色に量子化"
              checked={options.quantize}
              onChange={(value) => demo.update("quantize", value)}
            />
            <SwitchControl
              name="interpolate"
              label="計算セルの間を補間"
              checked={options.interpolate}
              onChange={(value) => demo.update("interpolate", value)}
            />
            <SwitchControl
              name="clock"
              label="時刻・日付を表示"
              checked={options.clock}
              onChange={(value) => demo.update("clock", value)}
            />
            <Selector
              name="font"
              label="時計フォント"
              value={options.font}
              choices={[
                ["leco", "LECO"],
                ["bitham", "Bitham"],
              ]}
              onChange={(value) =>
                demo.update("font", value as "leco" | "bitham")
              }
            />
            <Selector
              name="face"
              label="文字盤"
              value={options.face}
              choices={[
                ["digital", "デジタル"],
                ["analog", "アナログ"],
              ]}
              onChange={(value) => demo.update("face", value as ClockFace)}
            />
            <SwitchControl
              name="liveTime"
              label="現在時刻"
              checked={options.time === null}
              onChange={(value) =>
                demo.update("time", value ? null : currentMinute())
              }
            />
            {options.time !== null && (
              <NumberSlider
                name="time"
                label={`時刻 ${clockLabel(options.time)}`}
                value={options.time}
                min={0}
                max={1439}
                step={1}
                onChange={(value) => demo.update("time", value)}
              />
            )}
            <SwitchControl
              name="avoid"
              label={
                options.face === "analog" ? "針と数字を避ける" : "数字を避ける"
              }
              checked={options.avoid}
              disabled={!options.clock}
              onChange={(value) => demo.update("avoid", value)}
            />
            <SwitchControl
              name="actual"
              label="表示倍率 1×"
              checked={options.actual}
              onChange={(value) => demo.update("actual", value)}
            />
          </section>
          <section className={s.panel} aria-labelledby="save-title">
            <h2 className={s.heading} id="save-title">
              保存
            </h2>
            <div className={s.row}>
              <button
                className={s.button}
                type="button"
                data-save="json"
                onClick={() => demo.download("json")}
              >
                設定 JSON
              </button>
              <button
                className={s.button}
                type="button"
                data-save="h"
                disabled={isFloat(options.engine)}
                onClick={() => demo.download("h")}
              >
                Pebble config.h
              </button>
              <button
                className={s.button}
                type="button"
                data-save="png"
                onClick={() => demo.download("png")}
              >
                PNG
              </button>
            </div>
            <output className={s.note} data-notice aria-live="polite">
              {demo.notice}
            </output>
            <details className={s.details}>
              <summary>現在の設定</summary>
              <pre className={s.pre} data-settings>
                {JSON.stringify(makeSettings(demo.settingsInput), null, 2)}
              </pre>
              <p className={s.fine}>
                JSON は設定の記録です。濃度場と手描きの種は含みません。
              </p>
            </details>
          </section>
        </aside>
      </div>
    </main>
  );
}
