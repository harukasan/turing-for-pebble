import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { presets } from "../../lib/simulation.ts";
import { detectLang, langOf, LANGS, STRINGS } from "./i18n.ts";
import { DIFFUSION_PRESETS } from "./settings.ts";

test("every language has the same strings and list lengths", () => {
  const [base, ...rest] = LANGS.map((lang) => STRINGS[lang]);
  for (const strings of rest) {
    assert.deepEqual(Object.keys(strings).sort(), Object.keys(base).sort());
    assert.equal(strings.stopLabels.length, 4);
  }
  for (const lang of LANGS) {
    assert.equal(STRINGS[lang].presets.length, presets.length);
    assert.equal(STRINGS[lang].widths.length, DIFFUSION_PRESETS.length);
  }
});

test("every data-i18n name of the page is a plain string", () => {
  const html = readFileSync(new URL("index.html", import.meta.url), "utf8");
  const names = [
    ...html.matchAll(/data-i18n(?:-aria|-placeholder)?="([^"]+)"/g),
  ].map((match) => match[1]);
  assert(names.length > 0);
  for (const lang of LANGS)
    for (const name of names)
      assert.equal(
        typeof (STRINGS[lang] as Record<string, unknown>)[name],
        "string",
        `${lang}.${name}`
      );
  assert(!/[぀-ヿ一-鿿]/.test(html), "no fixed Japanese");
});

test("the phone's language picks Japanese or English", () => {
  assert.equal(detectLang("ja-JP"), "ja");
  assert.equal(detectLang("JA"), "ja");
  assert.equal(detectLang("en-US"), "en");
  assert.equal(detectLang("fr"), "en");
  assert.equal(detectLang(undefined), "en");
  assert.equal(langOf("ja"), "ja");
  assert.equal(langOf("fr"), null);
  assert.equal(langOf(null), null);
});
