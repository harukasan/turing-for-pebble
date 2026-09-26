/*
 * PebbleKit JS of the watchface. It opens the settings page, sends the
 * settings the page returns to the watch, and keeps a copy that it sends
 * again when the face starts, for example after a reinstall.
 *
 * scripts/embed-config.mjs prepends RD_PAGE_HTML, the settings page with
 * its preview as one HTML file, and RD_CONFIG_URL, the address of a hosted
 * copy of the page from RD_BUILD_CONFIG_URL or an empty string, and writes
 * the result to src/pkjs/generated/pebble-js-app.js. The script keeps to
 * ES5 for the phone's JavaScript engines.
 */
/* global Pebble, RD_PAGE_HTML, RD_CONFIG_URL, localStorage, console */

var STORAGE_KEY = "settings";
/* The settings and their largest values, in the order of the message keys
 * of package.json. A message key is the setting name in capitals. Every
 * value is from 0, or 2 for the number of stops, to its largest. */
var SETTINGS = [
  ["feed", 32768],
  ["kill", 32768],
  ["da", 32768],
  ["db", 32768],
  ["dt", 32768],
  ["palette", 9],
  ["low", 0xffffff],
  ["high", 0xffffff],
  ["stops", 4],
  ["mid1", 0xffffff],
  ["mid2", 0xffffff],
  ["font", 1],
  ["avoid", 1],
  ["clock", 1],
];

/* The settings of a value if every one is an integer in range, in key
 * order, or null. */
function settingsOf(value) {
  if (!value || typeof value !== "object") return null;
  var settings = {};
  for (var i = 0; i < SETTINGS.length; i++) {
    var name = SETTINGS[i][0];
    var v = value[name];
    var min = name === "stops" ? 2 : 0;
    if (typeof v !== "number" || v % 1 !== 0 || v < min || v > SETTINGS[i][1])
      return null;
    settings[name] = v;
  }
  return settings;
}

/* Settings stored before the custom palette had more than two stops lack
 * the number of stops and the middle stops: two stops, and middles that
 * are only used once the page adds stops. */
function load() {
  try {
    var value = JSON.parse(localStorage.getItem(STORAGE_KEY));
    if (value && typeof value === "object" && !("stops" in value)) {
      value.stops = 2;
      value.mid1 = value.low;
      value.mid2 = value.high;
    }
    return settingsOf(value);
  } catch (e) {
    return null;
  }
}

function send(settings) {
  var message = {};
  for (var i = 0; i < SETTINGS.length; i++) {
    var name = SETTINGS[i][0];
    message[name.toUpperCase()] = settings[name];
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
    var name = SETTINGS[i][0];
    parts.push(name + "=" + settings[name]);
  }
  return parts.join("&");
}

/* The embedded page as a data URL with the settings in it, or the hosted
 * page with the settings in its query string. Without stored settings the
 * page shows the defaults. */
function pageUrl(settings) {
  if (RD_CONFIG_URL) {
    if (!settings) return RD_CONFIG_URL;
    var separator = RD_CONFIG_URL.indexOf("?") < 0 ? "?" : "&";
    return RD_CONFIG_URL + separator + query(settings);
  }
  var html = settings
    ? RD_PAGE_HTML.replace("__SETTINGS__", function () {
        return JSON.stringify(settings);
      })
    : RD_PAGE_HTML;
  return "data:text/html;charset=utf-8," + encodeURIComponent(html);
}

/* The page's result: JSON, percent-encoded or not. */
function parse(response) {
  try {
    var text =
      response.charAt(0) === "{" ? response : decodeURIComponent(response);
    return settingsOf(JSON.parse(text));
  } catch (e) {
    return null;
  }
}

Pebble.addEventListener("ready", function () {
  var settings = load();
  if (settings) send(settings);
});

Pebble.addEventListener("showConfiguration", function () {
  Pebble.openURL(pageUrl(load()));
});

Pebble.addEventListener("webviewclosed", function (e) {
  if (!e || !e.response) return;
  var settings = parse(e.response);
  if (!settings) {
    console.log("The settings page returned invalid settings");
    return;
  }
  localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
  send(settings);
});
