// Summarize build/precision.jsonl from scripts/precision-sweep.sh as a
// Markdown table: per candidate and checkpoint, the mean and worst B mean
// absolute error, the lowest B correlation, and the mean A error over all
// preset and seed cases. Sorted by the B error at the last checkpoint.
import { readFileSync } from "node:fs";

const file = process.argv[2] ?? "build/precision.jsonl";
const rows = readFileSync(file, "utf8")
  .split("\n")
  .filter(Boolean)
  .map((line) => JSON.parse(line));
const steps = [...new Set(rows.map((r) => r.step))].sort((a, b) => a - b);
const configs = [...new Set(rows.map((r) => r.config))];
const mean = (values) => values.reduce((x, y) => x + y, 0) / values.length;
const last = steps.at(-1);

const summary = configs.map((config) => {
  const byStep = {};
  for (const step of steps) {
    const cases = rows.filter((r) => r.config === config && r.step === step);
    if (!cases.length) continue;
    byStep[step] = {
      n: cases.length,
      bMae: mean(cases.map((r) => r.b.mae)),
      bWorst: Math.max(...cases.map((r) => r.b.mae)),
      corrMin: Math.min(...cases.map((r) => r.b.corr)),
      aMae: mean(cases.map((r) => r.a.mae)),
    };
  }
  return { config, byStep };
});
summary.sort(
  (a, b) =>
    (a.byStep[last]?.bMae ?? Infinity) - (b.byStep[last]?.bMae ?? Infinity)
);

const header = ["Candidate", "Cases"];
for (const step of steps)
  header.push(
    `B MAE @${step}`,
    `Worst B MAE @${step}`,
    `Min B corr @${step}`,
    `A MAE @${step}`
  );
console.log(`| ${header.join(" | ")} |`);
console.log(`| ${header.map(() => "---").join(" | ")} |`);
for (const { config, byStep } of summary) {
  const cells = [`\`${config}\``, String(byStep[steps[0]]?.n ?? 0)];
  for (const step of steps) {
    const s = byStep[step];
    cells.push(
      s ? s.bMae.toFixed(5) : "-",
      s ? s.bWorst.toFixed(5) : "-",
      s ? s.corrMin.toFixed(3) : "-",
      s ? s.aMae.toFixed(4) : "-"
    );
  }
  console.log(`| ${cells.join(" | ")} |`);
}
