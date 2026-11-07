// ==UserScript==
// @name         User Agent 
// @version      1.0
// @description  Change the User Agent to Android Chrome
// ==/UserScript==

Object.defineProperty(navigator, 'userAgent', {
    value: 'Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Mobile Safari/537.3'
});