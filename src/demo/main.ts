import "./style.css";
import { mountDemo } from "./mount";

const root = document.getElementById("pebble-turing-demo");
if (!root) throw new Error("Turing Pattern Face demo root is missing");
mountDemo(root, { assetBaseUrl: import.meta.env.BASE_URL });
