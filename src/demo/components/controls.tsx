/** @jsxImportSource react */
import { useEffect, useId, useState, type CSSProperties } from "react";
import { type Engine, type Model } from "../../core";
import { type SliderKey } from "../../core/modes";
import { styles as s } from "../styles";

export { parameterBounds as bounds, type SliderKey } from "../../core/modes";

export const labels: { [M in Model]: Record<SliderKey<M>, string> } = {
  "gray-scott": {
    feed: "Feed / A の供給",
    kill: "Kill / B の除去",
    da: "Da / A の拡散",
    db: "Db / B の拡散",
    dt: "dt / 時間刻み",
  },
  fhn: {
    du: "Du / u の拡散",
    dv: "Dv / v の拡散",
    ru: "ru / u の反応速度",
    rv: "rv / v の反応速度",
    av: "av / v の減衰",
    k: "k / u の偏り",
    dt: "dt / 時間刻み",
  },
};

export const engines: [Engine, string][] = [
  ["q15-120", "Wasm Q15 / 120 × 136"],
  ["float-120", "Float32 / 120 × 136"],
];

export function NumberSlider({
  label,
  value,
  min,
  max,
  step,
  name,
  onChange,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step: number;
  name: string;
  onChange: (value: number) => void;
}) {
  const id = useId();
  const [draft, setDraft] = useState(String(value));
  useEffect(() => setDraft(String(value)), [value]);
  const progress = Math.round(((value - min) / (max - min)) * 100000) / 1000;
  const sliderStyle = { "--slider-progress": `${progress}%` } as CSSProperties;

  function commit() {
    const next = Number(draft);
    if (
      draft.trim() !== "" &&
      Number.isFinite(next) &&
      next >= min &&
      next <= max &&
      (step !== 1 || Number.isInteger(next))
    ) {
      onChange(next);
      setDraft(String(next));
    } else {
      setDraft(String(value));
    }
  }

  return (
    <div className={s.rangeControl}>
      <div className={s.rangeTop}>
        <label id={`${id}-label`} htmlFor={`${id}-number`}>
          {label}
        </label>
        <input
          className={`${s.field} ${s.rangeNumber}`}
          id={`${id}-number`}
          type="number"
          data-param-number={name}
          min={min}
          max={max}
          step={step}
          value={draft}
          onChange={(event) => setDraft(event.currentTarget.value)}
          onBlur={commit}
          onKeyDown={(event) => {
            if (event.key === "Enter") event.currentTarget.blur();
          }}
        />
      </div>
      <input
        className={s.slider}
        type="range"
        data-param={name}
        aria-labelledby={`${id}-label`}
        min={min}
        max={max}
        step={step}
        value={value}
        style={sliderStyle}
        onChange={(event) => onChange(event.currentTarget.valueAsNumber)}
      />
    </div>
  );
}

export function SwitchControl({
  name,
  label,
  checked,
  disabled = false,
  onChange,
}: {
  name: string;
  label: string;
  checked: boolean;
  disabled?: boolean;
  onChange: (checked: boolean) => void;
}) {
  return (
    <label className={s.switchLabel} aria-disabled={disabled}>
      <input
        className={s.switchInput}
        type="checkbox"
        role="switch"
        data-option={name}
        checked={checked}
        disabled={disabled}
        onChange={(event) => onChange(event.currentTarget.checked)}
      />
      <span>{label}</span>
    </label>
  );
}

export function Selector({
  name,
  label,
  value,
  choices,
  onChange,
}: {
  name: string;
  label: string;
  value: string;
  choices: [string, string][];
  onChange: (value: string) => void;
}) {
  return (
    <label className={s.label}>
      {label}
      <span className={s.selectWrap}>
        <select
          className={s.select}
          data-option={name}
          value={value}
          onChange={(event) => onChange(event.currentTarget.value)}
        >
          {choices.map(([key, text]) => (
            <option key={key} value={key}>
              {text}
            </option>
          ))}
        </select>
        <svg
          className={s.selectArrow}
          viewBox="0 0 16 16"
          fill="none"
          aria-hidden="true"
        >
          <path
            d="m3.5 6 4.5 4 4.5-4"
            stroke="currentColor"
            strokeWidth="1.5"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </svg>
      </span>
    </label>
  );
}

export function SeedField({
  value,
  onChange,
}: {
  value: number;
  onChange: (value: number) => void;
}) {
  const [draft, setDraft] = useState(String(value));
  useEffect(() => setDraft(String(value)), [value]);
  function commit() {
    const next = Number(draft);
    if (
      draft.trim() !== "" &&
      Number.isInteger(next) &&
      next >= 0 &&
      next <= 4294967295
    ) {
      onChange(next);
    } else {
      setDraft(String(value));
    }
  }
  return (
    <label className={s.label}>
      乱数シード
      <input
        className={s.field}
        type="number"
        data-option="seed"
        min="0"
        max="4294967295"
        step="1"
        value={draft}
        onChange={(event) => setDraft(event.currentTarget.value)}
        onBlur={commit}
        onKeyDown={(event) => {
          if (event.key === "Enter") event.currentTarget.blur();
        }}
      />
    </label>
  );
}
