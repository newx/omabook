// Map/WeakMap "upsert" methods, used by the pdf.js bundled inside foliate-js.
//
// `getOrInsertComputed` is a recent TC39 proposal that the Chromium inside
// QtWebEngine does not implement yet, so opening any PDF fails with
// "getOrInsertComputed is not a function". Patching the vendored pdf.js would
// be undone by the next `git pull` of foliate-js; this is applied from outside
// instead, and can simply be deleted once the engine ships the methods.
(function () {
  "use strict";

  const define = (proto, name, fn) => {
    if (typeof proto[name] === "function") return;
    Object.defineProperty(proto, name, {
      value: fn,
      writable: true,
      enumerable: false,
      configurable: true,
    });
  };

  for (const proto of [Map.prototype, WeakMap.prototype]) {
    define(proto, "getOrInsert", function (key, value) {
      if (!this.has(key)) this.set(key, value);
      return this.get(key);
    });

    define(proto, "getOrInsertComputed", function (key, callback) {
      if (!this.has(key)) this.set(key, callback(key));
      return this.get(key);
    });
  }
})();
