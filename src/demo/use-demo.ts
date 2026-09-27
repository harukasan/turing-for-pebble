import { useEffect, useReducer, useRef, useState } from "react";
import {
  presetById,
  presetParameters,
  presets,
  withRestingPoint,
  type Parameters,
} from "../../lib/simulation";
import {
  TuringPlayer,
  defaultParameters,
  defaultParametersFor,
  defaultPlayerSettings,
  engineSupportsModel,
  isFloat,
  type Engine,
  type Model,
  type ParameterKey,
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

/** Options with new parameters, and the watch engine if the current engine
 * does not run their model. */
const withModel = (options: DemoOptions, params: Parameters): DemoOptions => ({
  ...options,
  params,
  engine: engineSupportsModel(options.engine, params.model)
    ? options.engine
    : "q15-120",
});

export function useDemo(assetBaseUrl: string) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const playerRef = useRef<TuringPlayer | null>(null);
  const [options, setOptions] = useState(initialOptions);
  // The latest options, for the browser tool registered once.
  const latest = useRef(options);
  latest.current = options;
  const [stats, setStats] = useState(emptyStats);
  const [notice, setNotice] = useState("");
  const [resetVersion, requestReset] = useReducer(
    (value: number) => value + 1,
    0
  );
  /** A pending reset for a parameter of the initial field, so a dragged
   * slider restarts the field once it rests instead of on every input. */
  const initialReset = useRef(0);
  /** Reset now, dropping a pending reset that would reset the new field
   * again. */
  const resetField = () => {
    clearTimeout(initialReset.current);
    requestReset();
  };

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

  // A reset uses the parameters of the same render, so a model change or a
  // preset that requests a reset loads the matching field.
  useEffect(() => {
    void playerRef.current?.load(options.engine, options.seed, options.params);
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
              "Choose a preset by id, or set Gray-Scott Feed and Kill (which selects the Gray-Scott model), in the visible simulation. A preset resets the field. Optionally reset to the current seed.",
            inputSchema: {
              type: "object",
              properties: {
                preset: {
                  type: "string",
                  enum: presets.map((preset) => preset.id),
                },
                feed: { type: "number", minimum: 0.01, maximum: 0.1 },
                kill: { type: "number", minimum: 0.03, maximum: 0.075 },
                reset: { type: "boolean" },
              },
              anyOf: [{ required: ["preset"] }, { required: ["feed", "kill"] }],
              additionalProperties: false,
            },
            annotations: { readOnlyHint: false },
            execute(input: unknown) {
              if (events.signal.aborted)
                throw new Error("Demo has been destroyed");
              if (!input || typeof input !== "object")
                throw new Error("Expected an object");
              const value = input as Record<string, unknown>;
              const number = (key: string, min: number, max: number) =>
                typeof value[key] === "number" &&
                Number.isFinite(value[key]) &&
                (value[key] as number) >= min &&
                (value[key] as number) <= max;
              const preset =
                typeof value.preset === "string"
                  ? presetById(value.preset)
                  : undefined;
              const coefficients =
                value.feed !== undefined || value.kill !== undefined;
              if (
                Object.keys(value).some(
                  (key) => !["preset", "feed", "kill", "reset"].includes(key)
                ) ||
                (value.preset !== undefined && !preset) ||
                (!preset && !coefficients) ||
                (coefficients &&
                  (!number("feed", 0.01, 0.1) ||
                    !number("kill", 0.03, 0.075) ||
                    (preset && preset.model !== "gray-scott"))) ||
                (value.reset !== undefined && typeof value.reset !== "boolean")
              )
                throw new Error("Invalid parameters");
              const current = latest.current.params;
              let params = preset ? presetParameters(preset) : current;
              if (coefficients) {
                const gray =
                  params.model === "gray-scott"
                    ? params
                    : defaultParametersFor("gray-scott");
                params = {
                  ...gray,
                  feed: value.feed as number,
                  kill: value.kill as number,
                };
              }
              const next = params;
              setOptions((options) => withModel(options, next));
              const reset =
                value.reset === true ||
                !!preset ||
                params.model !== current.model;
              if (reset) resetField();
              return {
                preset: preset?.id,
                model: params.model,
                feed: coefficients ? value.feed : undefined,
                kill: coefficients ? value.kill : undefined,
                reset,
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

  /** Coefficients apply to the next step. The FitzHugh-Nagumo resting
   * value and initial condition shape the initial field, so changing them
   * resets it. */
  useEffect(() => () => clearTimeout(initialReset.current), []);

  function updateParam(key: ParameterKey, value: number) {
    setOptions((current) => ({
      ...current,
      params: withRestingPoint({
        ...current.params,
        [key]: value,
      } as Parameters),
    }));
    // The FitzHugh-Nagumo initial field is u = rest and v = rest / av, with
    // disks or the cut wave by init, and rest follows k and av.
    if (key === "init" || key === "k" || key === "av") {
      clearTimeout(initialReset.current);
      initialReset.current = window.setTimeout(requestReset, 300);
    }
  }

  /** Switch to another model with its default parameters and reset. */
  function chooseModel(model: Model) {
    if (model === latest.current.params.model) return;
    setOptions((current) => withModel(current, defaultParametersFor(model)));
    resetField();
  }

  function choosePreset(id: string) {
    const preset = presetById(id);
    if (!preset) return;
    setOptions((current) => withModel(current, presetParameters(preset)));
    resetField();
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
    chooseModel,
    choosePreset,
    requestReset: resetField,
    download,
  };
}
