// ==UserScript==
// @name         globalThis 
// @version      1.0
// @description  globalThis polyfill    
// @match        <all_urls>
// ==/UserScript==

(function () {
    if (typeof globalThis === "object") {
        return;
    }
    if (typeof window !== "undefined") {
        window.globalThis = window;
    } else if (typeof self !== "undefined") {
        self.globalThis = self;
    }
})();