import { createElement } from "react";
import { createRoot } from "react-dom/client";
import { DemoPage } from "./components/demo-page";

type MountOptions = { assetBaseUrl?: string };

export function mountDemo(root: HTMLElement, options: MountOptions = {}) {
  const reactRoot = createRoot(root);
  reactRoot.render(createElement(DemoPage, options));
  return { destroy: () => reactRoot.unmount() };
}
