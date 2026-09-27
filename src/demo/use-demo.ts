import { useEffect, useReducer, useRef, useState } from "react";
import { presets, type Parameters } from "../../lib/simulation";
import {
  TuringPlayer,
  defaultParameters,
  defaultPlayerSettings,
  isFloat,
  type Engine,
  type PlayerSettings,
  type PlayerStats,
} from "../core";
import { makeHeader, makeSettings, type SettingsInput } from "./serialization";

type DemoOptions = PlayerSettings & {
  engine: Engine;
  seed: number;
  running: boolean;
  actual: boolean;
};

function playerSettings(options: DemoOptions): PlayerSettings {
  return {
    params: options.params,
    speed: options.speed,
    palette: options.palette,
    quantize: options.quantize,
    interpolate: options.interpolate,
    clock: options.clock,
    font: options.font,
    face: options.face,
    time: options.time,
    avoid: options.avoid,
  };
}

const initialOptions = (): DemoOptions => ({
  ...defaultPlayerSettings,
  params: { ...defaultParameters },
  engine: "q15-120",
  seed: 42,
  running: !matchMedia("(prefers-reduced-motion: reduce)").matches,
  actual: false,
});

const emptyStats: PlayerStats = { steps: 0, stepMs: 0, bytes: null };

export function useDemo(assetBaseUrl: string) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const playerRef = useRef<TuringPlayer | null>(null);
  const [options, setOptions] = useState(initialOptions);
  const [stats, setStats] = useState(emptyStats);
  const [notice, setNotice] = useState("");
  const [resetVersion, requestReset] = useReducer(
    (value: number) => value + 1,
    0
  );

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const player = new TuringPlayer(
      canvas,
      assetBaseUrl,
      playerSettings(options),
      {
        onStats: setStats,
        onLoading: (loading) => setNotice(loading ? "読み込み中…" : ""),
        onError: (error, phase) => {
          setOptions((current) => ({ ...current, running: false }));
          setNotice(
            phase === "canvas"
              ? "Canvas 2D を利用できません。別のブラウザで開いてください。"
              : `${phase === "load" ? "読み込み" : "計算"}失敗: ${String(error)}`
          );
        },
      }
    );
    playerRef.current = player;
    return () => {
      player.dispose();
      playerRef.current = null;
    };
  }, [assetBaseUrl]);

  useEffect(() => {
    playerRef.current?.setSettings(playerSettings(options));
  }, [
    assetBaseUrl,
    options.params,
    options.speed,
    options.palette,
    options.quantize,
    options.interpolate,
    options.clock,
    options.font,
    options.face,
    options.time,
    options.avoid,
  ]);

  useEffect(() => {
    if (options.running) playerRef.current?.play();
    else playerRef.current?.pause();
  }, [assetBaseUrl, options.running]);

  useEffect(() => {
    void playerRef.current?.load(options.engine, options.seed);
  }, [assetBaseUrl, options.engine, options.seed, resetVersion]);

  useEffect(() => {
    const modelContext = (
      document as Document & {
        modelContext?: {
          registerTool: (
            tool: object,
            options: { signal: AbortSignal }
          ) => void | Promise<void>;
        };
      }
    ).modelContext;
    if (!modelContext?.registerTool) return;
    const events = new AbortController();
    try {
      Promise.resolve(
        modelContext.registerTool(
          {
            name: "configure_turing_pattern",
            description:
              "Set Feed and Kill in the visible simulation. Optionally reset to the current seed.",
            inputSchema: {
              type: "object",
              properties: {
                feed: { type: "number", minimum: 0.01, maximum: 0.1 },
                kill: { type: "number", minimum: 0.03, maximum: 0.075 },
                reset: { type: "boolean" },
              },
              required: ["feed", "kill"],
              additionalProperties: false,
            },
            annotations: { readOnlyHint: false },
            execute(input: unknown) {
              if (events.signal.aborted)
                throw new Error("Demo has been destroyed");
              if (!input || typeof input !== "object")
                throw new Error("Expected an object");
              const value = input as Record<string, unknown>;
              if (
                Object.keys(value).some(
                  (key) => !["feed", "kill", "reset"].includes(key)
                ) ||
                typeof value.feed !== "number" ||
                !Number.isFinite(value.feed) ||
                value.feed < 0.01 ||
                value.feed > 0.1 ||
                typeof value.kill !== "number" ||
                !Number.isFinite(value.kill) ||
                value.kill < 0.03 ||
                value.kill > 0.075 ||
                (value.reset !== undefined && typeof value.reset !== "boolean")
              )
                throw new Error("Invalid parameters");
              setOptions((current) => ({
                ...current,
                params: {
                  ...current.params,
                  feed: value.feed as number,
                  kill: value.kill as number,
                },
              }));
              if (value.reset) requestReset();
              return {
                feed: value.feed,
                kill: value.kill,
                reset: value.reset === true,
              };
            },
          },
          { signal: events.signal }
        )
      ).catch(() => {});
    } catch {
      // This browser API is optional.
    }
    return () => events.abort();
  }, []);

  function update<K extends keyof DemoOptions>(key: K, value: DemoOptions[K]) {
    setOptions((current) => ({ ...current, [key]: value }));
  }

  function updateParam(key: keyof Parameters, value: number) {
    setOptions((current) => ({
      ...current,
      params: { ...current.params, [key]: value },
    }));
  }

  function choosePreset(index: number) {
    const preset = presets[index];
    if (!preset) return;
    setOptions((current) => ({
      ...current,
      params: { ...defaultParameters, feed: preset.feed, kill: preset.kill },
    }));
    requestReset();
  }

  const settingsInput: SettingsInput = {
    params: options.params,
    engine: options.engine,
    seed: options.seed,
    palette: options.palette,
    quantize: options.quantize,
    interpolate: options.interpolate,
    clock: options.clock,
    font: options.font,
    face: options.face,
    avoid: options.avoid,
    steps: stats.steps,
  };

  function download(kind: "json" | "h" | "png") {
    if (kind === "h" && isFloat(options.engine)) return;
    const link = document.createElement("a");
    if (kind === "png") {
      if (!canvasRef.current) return;
      link.href = canvasRef.current.toDataURL("image/png");
      link.download = `turing-${options.seed}-${stats.steps}.png`;
    } else {
      const contents =
        kind === "json"
          ? JSON.stringify(makeSettings(settingsInput), null, 2)
          : makeHeader(settingsInput);
      const url = URL.createObjectURL(
        new Blob([contents], {
          type: kind === "json" ? "application/json" : "text/plain",
        })
      );
      link.href = url;
      link.download =
        kind === "h"
          ? "config.h"
          : `turing-${options.seed}-${stats.steps}.json`;
      setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
    link.click();
    setNotice(
      kind === "png"
        ? "PNG を保存しました。"
        : kind === "h"
          ? "Pebble 用 config.h を保存しました。"
          : "設定 JSON を保存しました。"
    );
  }

  return {
    canvasRef,
    playerRef,
    options,
    stats,
    notice,
    settingsInput,
    update,
    updateParam,
    choosePreset,
    requestReset,
    download,
  };
}
