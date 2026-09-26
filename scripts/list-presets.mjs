// Print the presets of lib/presets.ts as JSON for the C harnesses: the id,
// the model number of the core (RD_MODEL_*), and the Q15 parameter vector
// of RD_PARAM_MAX entries that rd_init_model takes.
//
//   node --experimental-transform-types scripts/list-presets.mjs
import { modelIndex, parameterVector, presets } from "../lib/presets.ts";

process.stdout.write(
  JSON.stringify(
    presets.map((preset) => ({
      id: preset.id,
      model: modelIndex[preset.model],
      q15: parameterVector(preset),
    }))
  ) + "\n"
);
