/*
 * PebbleKit JS of the watchface. It opens the settings page, sends the
 * settings the page returns to the watch, and keeps a copy that it sends
 * again when the face starts, for example after a reinstall. It also keeps
 * the language chosen on the page, which stays on the phone.
 *
 * scripts/embed-config.mjs prepends RD_PAGE_HTML, the settings page with
 * its preview as one HTML file, and RD_CONFIG_URL, the address of a hosted
 * copy of the page from RD_BUILD_CONFIG_URL or an empty string, and
 * RD_PARAM_RANGES, the parameter ranges of each model, and
 * RD_GRAY_SCOTT_NAMES, the Gray-Scott coefficient names of settings stored
 * before the model, both from lib/presets.ts, and writes
 * the result to src/pkjs/generated/pebble-js-app.js. The script keeps to
 * ES5 for the phone's JavaScript engines.
 */
/* global Pebble, RD_PAGE_HTML, RD_CONFIG_URL, RD_PARAM_RANGES, RD_GRAY_SCOTT_NAMES, localStorage, console */

var STORAGE_KEY = "settings";
var LANGUAGE_KEY = "language";
var LANGUAGES = ["en", "ja"];
/* The settings in the order of the message keys of package.json. A message
 * key is the setting name in capitals. The model is 0 (Gray-Scott) or 1
 * (FitzHugh-Nagumo), p0 to p9 its Q15 parameter vector, whose entries after
 * the model's own are 0, and every other setting is an integer from 0, or 2
 * for the number of stops, to its largest value. */
var PARAMS = 10;
/* RD_PARAM_RANGES, by model number, holds the range of each entry of the
 * model's vector as the watch checks it (parameterRanges of
 * lib/presets.ts). */
var MODELS = RD_PARAM_RANGES.length;
var FIXED = [
  ["palette", 9],
  ["low", 0xffffff],
  ["high", 0xffffff],
  ["stops", 4],
  ["mid1", 0xffffff],
  ["mid2", 0xffffff],
  ["font", 1],
  ["avoid", 1],
  ["clock", 1],
  ["date", 1],
  ["face", 1],
];
var SETTINGS = ["model"];
for (var p = 0; p < PARAMS; p++) SETTINGS.push("p" + p);
for (var f = 0; f < FIXED.length; f++) SETTINGS.push(FIXED[f][0]);

function isInt(v, min, max) {
  return typeof v === "number" && v % 1 === 0 && v >= min && v <= max;
}

/* The settings of a value if every one is an integer in range, in key
 * order, or null. */
function settingsOf(value) {
  if (!value || typeof value !== "object") return null;
  if (!isInt(value.model, 0, MODELS - 1)) return null;
  var settings = { model: value.model };
  var ranges = RD_PARAM_RANGES[value.model];
  for (var i = 0; i < PARAMS; i++) {
    var v = value["p" + i];
    var ok = i < ranges.length ? isInt(v, ranges[i][0], ranges[i][1]) : v === 0;
    if (!ok) return null;
    settings["p" + i] = v;
  }
  for (var j = 0; j < FIXED.length; j++) {
    var name = FIXED[j][0];
    var min = name === "stops" ? 2 : 0;
    if (!isInt(value[name], min, FIXED[j][1])) return null;
    settings[name] = value[name];
  }
  return settings;
}

/* Settings stored before the custom palette had more than two stops lack
 * the number of stops and the middle stops: two stops, and middles that
 * are only used once the page adds stops. Those stored before the date
 * could be hidden show it, those stored before the analog face show the
 * digital one, and those stored before the model carry the Gray-Scott
 * coefficients by name. */
function load() {
  try {
    var value = JSON.parse(localStorage.getItem(STORAGE_KEY));
    if (value && typeof value === "object" && !("stops" in value)) {
      value.stops = 2;
      value.mid1 = value.low;
      value.mid2 = value.high;
    }
    if (value && typeof value === "object" && !("date" in value)) {
      value.date = 1;
    }
    if (value && typeof value === "object" && !("face" in value)) {
      value.face = 0;
    }
    if (value && typeof value === "object" && !("model" in value)) {
      value.model = 0;
      var names = RD_GRAY_SCOTT_NAMES;
      for (var i = 0; i < PARAMS; i++)
        value["p" + i] = i < names.length ? value[names[i]] : 0;
    }
    return settingsOf(value);
  } catch (e) {
    return null;
  }
}

/* The page's language as stored, or null for the phone's own. */
function language() {
  try {
    var lang = localStorage.getItem(LANGUAGE_KEY);
    return LANGUAGES.indexOf(lang) < 0 ? null : lang;
  } catch (e) {
    return null;
  }
}

function send(settings) {
  var message = {};
  for (var i = 0; i < SETTINGS.length; i++) {
    message[SETTINGS[i].toUpperCase()] = settings[SETTINGS[i]];
  }
  Pebble.sendAppMessage(
    message,
    function () {},
    function (e) {
      console.log("Settings were not delivered: " + JSON.stringify(e));
    }
  );
}

function query(settings) {
  var parts = [];
  for (var i = 0; i < SETTINGS.length; i++) {
    parts.push(SETTINGS[i] + "=" + settings[SETTINGS[i]]);
  }
  return parts.join("&");
}

/* The embedded page as a data URL with the settings and the language in
 * it, or the hosted page with them in its query string. Without stored
 * settings the page shows the defaults, and without a stored language it
 * follows the phone's. */
function pageUrl(settings, lang) {
  if (RD_CONFIG_URL) {
    var parts = [];
    if (settings) parts.push(query(settings));
    if (lang) parts.push("lang=" + lang);
    if (!parts.length) return RD_CONFIG_URL;
    var separator = RD_CONFIG_URL.indexOf("?") < 0 ? "?" : "&";
    return RD_CONFIG_URL + separator + parts.join("&");
  }
  var embedded = settings || {};
  if (lang) embedded.lang = lang;
  var html =
    settings || lang
      ? RD_PAGE_HTML.replace("__SETTINGS__", function () {
          return JSON.stringify(embedded);
        })
      : RD_PAGE_HTML;
  return "data:text/html;charset=utf-8," + encodeURIComponent(html);
}

/* The page's result: JSON, percent-encoded or not, or null. */
function parse(response) {
  try {
    var text =
      response.charAt(0) === "{" ? response : decodeURIComponent(response);
    var value = JSON.parse(text);
    return value && typeof value === "object" ? value : null;
  } catch (e) {
    return null;
  }
}

Pebble.addEventListener("ready", function () {
  var settings = load();
  if (settings) send(settings);
});

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(pageUrl(load(), language()));
});

Pebble.addEventListener("webviewclosed", function (e) {
  if (!e || !e.response) return;
  var result = parse(e.response);
  var settings = settingsOf(result);
  if (!settings) {
    console.log("The settings page returned invalid settings");
    return;
  }
  localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
  if (LANGUAGES.indexOf(result.lang) >= 0)
    localStorage.setItem(LANGUAGE_KEY, result.lang);
  send(settings);
});
