import eslint from "@eslint/js";
import typescriptParser from "@typescript-eslint/parser";
import panda from "@pandacss/eslint-plugin";
import tseslint from "typescript-eslint";
import prettier from "eslint-config-prettier/flat";

export default tseslint.config([
  {
    extends: [eslint.configs.recommended, ...tseslint.configs.recommended],
    files: ["**/*.js", "**/*.jsx", "**/*.ts", "**/*.tsx"],
    ignores: ["**/*.d.ts", "dist/**/*", "styled-system/**/*"],
    plugins: {
      "@pandacss": panda,
    },
    languageOptions: {
      parser: typescriptParser,
    },
    rules: {
      ...panda.configs.recommended.rules,
    },
  },
  {
    // PebbleKit JS runs in the phone apps' JavaScript engines, so it keeps
    // to ES5: var and catch bindings.
    files: ["pebble/src/pkjs/**/*.js"],
    rules: {
      "no-var": "off",
      "@typescript-eslint/no-unused-vars": ["error", { caughtErrors: "none" }],
    },
  },
  prettier,
]);
