
// ==UserScript==
// @name         Polyfill EdgeHTML/15
// @version      1.0
// @description  Polyfill for EdgeHTML/15.15254
// @match        <all_urls>
// ==/UserScript==

/* @ungap/global-this */
!function(t){function e(){var e=this||self;e.globalThis=e,delete t.prototype._T_}"object"!=typeof globalThis&&(this?e():(t.defineProperty(t.prototype,"_T_",{configurable:!0,get:e}),_T_))}(Object);

/* @ungap/assign */
/*! (c) Andrea Giammarchi - ISC */
var assign=Object.assign||function(r){for(var n,a=1;a<arguments.length;a++)for(var s in n=arguments[a]||{})n.hasOwnProperty(s)&&(r[s]=n[s]);return r};

/* @ungap/at */
((classes, defineProperty) => {
  for (let proto, i = 0; i < classes.length; i++) {
    proto = classes[i].prototype;

    if (!('at' in proto)) {
      defineProperty(proto, 'at', {
        configurable: true,
        writable: true,
        /**
         * @param {number} index
         * @returns {unknown}
         */
        value(index) {
          return index < 0 ? this[this.length + index] : this[index];
        }
      });
    }
  }
})([Array, String], Object.defineProperty);


/* @ungap/custom-elements */
/*! (c) Andrea Giammarchi @webreflection ISC */
!function(){"use strict";var e=function(e,t){var n=function(e){for(var t=0,n=e.length;t<n;t++)r(e[t])},r=function(e){var t=e.target,n=e.attributeName,r=e.oldValue;t.attributeChangedCallback(n,r,t.getAttribute(n))};return function(o,a){var l=o.constructor.observedAttributes;return l&&e(a).then((function(){new t(n).observe(o,{attributes:!0,attributeOldValue:!0,attributeFilter:l});for(var e=0,a=l.length;e<a;e++)o.hasAttribute(l[e])&&r({target:o,attributeName:l[e],oldValue:null})})),o}};function t(e,t){(null==t||t>e.length)&&(t=e.length);for(var n=0,r=new Array(t);n<t;n++)r[n]=e[n];return r}function n(e,n){var r="undefined"!=typeof Symbol&&e[Symbol.iterator]||e["@@iterator"];if(!r){if(Array.isArray(e)||(r=function(e,n){if(e){if("string"==typeof e)return t(e,n);var r=Object.prototype.toString.call(e).slice(8,-1);return"Object"===r&&e.constructor&&(r=e.constructor.name),"Map"===r||"Set"===r?Array.from(e):"Arguments"===r||/^(?:Ui|I)nt(?:8|16|32)(?:Clamped)?Array$/.test(r)?t(e,n):void 0}}(e))||n&&e&&"number"==typeof e.length){r&&(e=r);var o=0,a=function(){};return{s:a,n:function(){return o>=e.length?{done:!0}:{done:!1,value:e[o++]}},e:function(e){throw e},f:a}}throw new TypeError("Invalid attempt to iterate non-iterable instance.\nIn order to be iterable, non-array objects must have a [Symbol.iterator]() method.")}var l,i=!0,c=!1;return{s:function(){r=r.call(e)},n:function(){var e=r.next();return i=e.done,e},e:function(e){c=!0,l=e},f:function(){try{i||null==r.return||r.return()}finally{if(c)throw l}}}}
/*! (c) Andrea Giammarchi - ISC */var r=!0,o=!1,a="querySelectorAll",l="querySelectorAll",i=self,c=i.document,u=i.Element,s=i.MutationObserver,f=i.Set,d=i.WeakMap,h=function(e){return l in e},v=[].filter,p=function(e){var t=new d,i=function(n,r){var o;if(r)for(var a,l=function(e){return e.matches||e.webkitMatchesSelector||e.msMatchesSelector}(n),i=0,c=y.length;i<c;i++)l.call(n,a=y[i])&&(t.has(n)||t.set(n,new f),(o=t.get(n)).has(a)||(o.add(a),e.handle(n,r,a)));else t.has(n)&&(o=t.get(n),t.delete(n),o.forEach((function(t){e.handle(n,r,t)})))},p=function(e){for(var t=!(arguments.length>1&&void 0!==arguments[1])||arguments[1],n=0,r=e.length;n<r;n++)i(e[n],t)},y=e.query,g=e.root||c,m=function(e){var t=arguments.length>1&&void 0!==arguments[1]?arguments[1]:document,l=arguments.length>2&&void 0!==arguments[2]?arguments[2]:MutationObserver,i=arguments.length>3&&void 0!==arguments[3]?arguments[3]:["*"],c=function t(o,l,i,c,u,s){var f,d=n(o);try{for(d.s();!(f=d.n()).done;){var h=f.value;(s||a in h)&&(u?i.has(h)||(i.add(h),c.delete(h),e(h,u)):c.has(h)||(c.add(h),i.delete(h),e(h,u)),s||t(h[a](l),l,i,c,u,r))}}catch(e){d.e(e)}finally{d.f()}},u=new l((function(e){if(i.length){var t,a=i.join(","),l=new Set,u=new Set,s=n(e);try{for(s.s();!(t=s.n()).done;){var f=t.value,d=f.addedNodes,h=f.removedNodes;c(h,a,l,u,o,o),c(d,a,l,u,r,o)}}catch(e){s.e(e)}finally{s.f()}}})),s=u.observe;return(u.observe=function(e){return s.call(u,e,{subtree:r,childList:r})})(t),u}(i,g,s,y),w=u.prototype.attachShadow;return w&&(u.prototype.attachShadow=function(e){var t=w.call(this,e);return m.observe(t),t}),y.length&&p(g[l](y)),{drop:function(e){for(var n=0,r=e.length;n<r;n++)t.delete(e[n])},flush:function(){for(var e=m.takeRecords(),t=0,n=e.length;t<n;t++)p(v.call(e[t].removedNodes,h),!1),p(v.call(e[t].addedNodes,h),!0)},observer:m,parse:p}},y=self,g=y.document,m=y.Map,w=y.MutationObserver,b=y.Object,E=y.Set,S=y.WeakMap,A=y.Element,M=y.HTMLElement,O=y.Node,N=y.Error,C=y.TypeError,T=y.Reflect,q=b.defineProperty,D=b.keys,I=b.getOwnPropertyNames,P=b.setPrototypeOf,k=!self.customElements,L=function(e){for(var t=D(e),n=[],r=new E,o=t.length,a=0;a<o;a++){n[a]=e[t[a]];try{delete e[t[a]]}catch(e){r.add(a)}}return function(){for(var a=0;a<o;a++)r.has(a)||(e[t[a]]=n[a])}};if(k){var x=function(){var e=this.constructor;if(!$.has(e))throw new C("Illegal constructor");var t=$.get(e);if(W)return F(W,t);var n=H.call(g,t);return F(P(n,e.prototype),t)},H=g.createElement,$=new m,_=new m,j=new m,R=new m,V=[],U=p({query:V,handle:function(e,t,n){var r=j.get(n);if(t&&!r.isPrototypeOf(e)){var o=L(e);W=P(e,r);try{new r.constructor}finally{W=null,o()}}var a="".concat(t?"":"dis","connectedCallback");a in r&&e[a]()}}).parse,W=null,B=function(e){if(!_.has(e)){var t,n=new Promise((function(e){t=e}));_.set(e,{$:n,_:t})}return _.get(e).$},F=e(B,w);self.customElements={define:function(e,t){if(R.has(e))throw new N('the name "'.concat(e,'" has already been used with this registry'));$.set(t,e),j.set(e,t.prototype),R.set(e,t),V.push(e),B(e).then((function(){U(g.querySelectorAll(e))})),_.get(e)._(t)},get:function(e){return R.get(e)},whenDefined:B},q(x.prototype=M.prototype,"constructor",{value:x}),self.HTMLElement=x,g.createElement=function(e,t){var n=t&&t.is,r=n?R.get(n):R.get(e);return r?new r:H.call(g,e)},"isConnected"in O.prototype||q(O.prototype,"isConnected",{configurable:!0,get:function(){return!(this.ownerDocument.compareDocumentPosition(this)&this.DOCUMENT_POSITION_DISCONNECTED)}})}else if(k=!self.customElements.get("extends-br"))try{var z=function e(){return self.Reflect.construct(HTMLBRElement,[],e)};z.prototype=HTMLLIElement.prototype;var G="extends-br";self.customElements.define("extends-br",z,{extends:"br"}),k=g.createElement("br",{is:G}).outerHTML.indexOf(G)<0;var J=self.customElements,K=J.get,Q=J.whenDefined;self.customElements.whenDefined=function(e){var t=this;return Q.call(this,e).then((function(n){return n||K.call(t,e)}))}}catch(e){}if(k){var X=function(e){var t=ae.get(e);ve(t.querySelectorAll(this),e.isConnected)},Y=self.customElements,Z=g.createElement,ee=Y.define,te=Y.get,ne=Y.upgrade,re=T||{construct:function(e){return e.call(this)}},oe=re.construct,ae=new S,le=new E,ie=new m,ce=new m,ue=new m,se=new m,fe=[],de=[],he=function(e){return se.get(e)||te.call(Y,e)},ve=p({query:de,handle:function(e,t,n){var r=ue.get(n);if(t&&!r.isPrototypeOf(e)){var o=L(e);we=P(e,r);try{new r.constructor}finally{we=null,o()}}var a="".concat(t?"":"dis","connectedCallback");a in r&&e[a]()}}).parse,pe=p({query:fe,handle:function(e,t){ae.has(e)&&(t?le.add(e):le.delete(e),de.length&&X.call(de,e))}}).parse,ye=A.prototype.attachShadow;ye&&(A.prototype.attachShadow=function(e){var t=ye.call(this,e);return ae.set(this,t),t});var ge=function(e){if(!ce.has(e)){var t,n=new Promise((function(e){t=e}));ce.set(e,{$:n,_:t})}return ce.get(e).$},me=e(ge,w),we=null;I(self).filter((function(e){return/^HTML.*Element$/.test(e)})).forEach((function(e){var t=self[e];function n(){var e=this.constructor;if(!ie.has(e))throw new C("Illegal constructor");var n=ie.get(e),r=n.is,o=n.tag;if(r){if(we)return me(we,r);var a=Z.call(g,o);return a.setAttribute("is",r),me(P(a,e.prototype),r)}return oe.call(this,t,[],e)}q(n.prototype=t.prototype,"constructor",{value:n}),q(self,e,{value:n})})),g.createElement=function(e,t){var n=t&&t.is;if(n){var r=se.get(n);if(r&&ie.get(r).tag===e)return new r}var o=Z.call(g,e);return n&&o.setAttribute("is",n),o},Y.get=he,Y.whenDefined=ge,Y.upgrade=function(e){var t=e.getAttribute("is");if(t){var n=se.get(t);if(n)return void me(P(e,n.prototype),t)}ne.call(Y,e)},Y.define=function(e,t,n){if(he(e))throw new N("'".concat(e,"' has already been defined as a custom element"));var r,o=n&&n.extends;ie.set(t,o?{is:e,tag:o}:{is:"",tag:e}),o?(r="".concat(o,'[is="').concat(e,'"]'),ue.set(r,t.prototype),se.set(e,t),de.push(r)):(ee.apply(Y,arguments),fe.push(r=e)),ge(e).then((function(){o?(ve(g.querySelectorAll(r)),le.forEach(X,[r])):pe(g.querySelectorAll(r))})),ce.get(e)._(t)}}}();

/* @ungap/custom-event */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};self.CustomEvent="function"==typeof CustomEvent?CustomEvent:function(t){return e[t]=new e("").constructor[t],e;function e(t,e){e=e||{};var n=document.createEvent("CustomEvent");return n.initCustomEvent(t,!!e.bubbles,!!e.cancelable,e.detail),n}}("prototype");

/* @ungap/dom-iterable */
/*! (c) Andrea Giammarchi - ISC */
!function(e){for(var t,o=Symbol.iterator,i=e[o],l=0;l<e.length;l++)o in(t=e[l].prototype)||Object.defineProperty(t,o,{configurable:!0,writable:!0,value:i})}([CSSRuleList,HTMLCollection,NodeList,StyleSheetList]);

/* @ungap/element-matches */
/*! (c) Andrea Giammarchi - ISC */
var elementMatches=function(c){return"matches"in document.documentElement?function(e){return this.matches(e)}:function(e){var t=this;return(t.matchesSelector||t.webkitMatchesSelector||t.khtmlMatchesSelector||t.mozMatchesSelector||t.msMatchesSelector||t.oMatchesSelector||function(e){var t=this.parentNode;return!!t&&-1<c.call(t.querySelectorAll(e),this)}).call(t,e)}}([].indexOf);

/* @ungap/event */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.Event=new Event(".").constructor}catch(t){try{self.Event=new CustomEvent(".").constructor}catch(t){self.Event=function(t,e){e=e||{};var n=document.createEvent("Event"),c=!!e.bubbles,r=!!e.cancelable;n.initEvent(t,c,r);try{n.bubbles=c,n.cancelable=r}catch(n){}return n}}}

/* @ungap/event-target */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.EventTarget=(new EventTarget).constructor}catch(t){!function(t,e){var n=t.create,r=t.defineProperty,i=s.prototype;function s(){"use strict";e.set(this,n(null))}function a(t,e,n){r(t,e,{configurable:!0,writable:!0,value:n})}function o(t){var e=t.options;return e&&e.once&&t.target.removeEventListener(this.type,t.listener),"function"==typeof t.listener?t.listener.call(t.target,this):t.listener.handleEvent(this),this._stopImmediatePropagationFlag}a(i,"addEventListener",(function(t,n,r){for(var i=e.get(this),s=i[t]||(i[t]=[]),a=0,o=s.length;a<o;a++)if(s[a].listener===n)return;s.push({target:this,listener:n,options:r})})),a(i,"dispatchEvent",(function(t){var n=e.get(this)[t.type];return n&&(a(t,"target",this),a(t,"currentTarget",this),n.slice(0).some(o,t),delete t.currentTarget,delete t.target),!0})),a(i,"removeEventListener",(function(t,n){for(var r=e.get(this),i=r[t]||(r[t]=[]),s=0,a=i.length;s<a;s++)if(i[s].listener===n)return void i.splice(s,1)})),self.EventTarget=s}(Object,new WeakMap)}

/* @ungap/from-entries */
var fromEntries=Object.fromEntries||function(e){for(var r,n=(Array.isArray(e)?createEntries(e):"entries"in e?e.entries():e),t={};(r=n.next())&&!r.done;){var a=r.value;Object.defineProperty(t,a[0],{configurable:!0,enumerable:!0,writable:!0,value:a[1]})}return t};function createEntries(r){var n=-1;return{next:function(){var e=r.length<=++n;return{done:e,value:e?void 0:r[n]}}}}

/* @ungap/get-own-property-descriptors */
var getOwnPropertyDescriptors=function(r){var e=r.getOwnPropertyDescriptors;if(e)return e;var a=r.defineProperty,c=r.getOwnPropertyDescriptor,i=r.getOwnPropertyNames,s=r.getOwnPropertySymbols;return function(r){for(var e,t={},n=i(r).concat(s?s(r):[]),o=0,p=n.length;o<p;o++)e=n[o],a(t,e,{enumerable:!0,value:c(r,e)});return t}}(Object);

/* @ungap/has-own */
"hasOwn"in Object||(Object.hasOwn=Object.call.bind(Object.hasOwnProperty));

/* @ungap/is-array */
var isArray=Array.isArray||function(a){var n=a.call([]);return function(r){return a.call(r)===n}}({}.toString);

/* @ungap/map */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.Map=Map}catch(t){!function(n,i){"use strict";var t=i(s.prototype,{size:{configurable:!0,get:function(){return this._k.length}}});return t.clear=function(){var t=this._k.length;this._k.splice(0,t),this._v.splice(0,t)},t.delete=function(t){var i=r(this,t);return i&&(this._k.splice(n,1),this._v.splice(n,1)),i},t.entries=function(){return this._k.map(h,this._v)},t.forEach=function(n,s){this._k.forEach(function(t,i){n.call(s,this._v[i],t,this)},this)},t.get=function(t){return r(this,t)?this._v[n]:void 0},t.has=function(t){return r(this,t)},t.keys=function(){return this._k.slice(0)},t.set=function(t,i){return this._v[r(this,t)?n:this._k.push(t)-1]=i,this},t.values=function(){return this._v.slice(0)},self.Map=s;function s(t){i(this,{_k:{value:[]},_v:{value:[]}}),t&&t.forEach(e,this)}function e(t){this.set(t[0],t[1])}function r(t,i){return-1<(n=t._k.indexOf(i))}function h(t,i){return[t,this[i]]}}(0,Object.defineProperties)}

/* @ungap/set */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.Set=Set}catch(t){!function(n,i){"use strict";var t=i(e.prototype,{size:{configurable:!0,get:function(){return this._v.length}}});function e(t){i(this,{_v:{value:[]}}),t&&t.forEach(this.add,this)}function s(t,i){return-1<(n=t._v.indexOf(i))}function r(t){return[t,t]}t.add=function(t){return s(this,t)||this._v.push(t),this},t.clear=function(){var t=this._v.length;this._v.splice(0,t)},t.delete=function(t){return s(this,t)&&!!this._v.splice(n,1)},t.entries=function(){return this._v.map(r)},t.forEach=function(n,e){this._v.forEach(function(t,i){n.call(e,t,t,this)},this)},t.has=function(t){return s(this,t)},t.keys=t.values=function(){return this._v.slice(0)},self.Set=e}(0,Object.defineProperties)}

/* @ungap/own-keys */
var ownKeys=function(e){try{return Reflect.ownKeys}catch(t){var r=e.getOwnPropertyNames,n=e.getOwnPropertySymbols;return function(t){"use strict";return r(t).concat(n?n(t):[])}}}(Object);

/* @ungap/promise-any */
var any=(Promise.any||function(n){return new Promise(function(e,o,i,t){i=[],t=n.map(function(n,r){return Promise.resolve(n).then(e,function(n){return i[r]=n,--t||o({errors:i})})}).length})}).bind(Promise);

/* @ungap/promise-all-settled */
var allSettled=Promise.allSettled||function(t){"use strict";var e=this;return e.all(t.map(function(t){return e.resolve(t).then(this.$,this._)},{$:function(t){return{status:"fulfilled",value:t}},_:function(t){return{status:"rejected",reason:t}}}))};

/* @ungap/queue-microtask */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.queueMicrotask=queueMicrotask}catch(e){try{e=Promise.resolve(),self.queueMicrotask=e.then.bind(e)}catch(e){self.queueMicrotask=function(e){setTimeout(e,0)}}}

/* @ungap/random-uuid */
if (typeof crypto === 'undefined')
  var crypto = require('crypto');

if (!('randomUUID' in crypto))
  // https://stackoverflow.com/a/2117523/2800218
  // LICENSE: https://creativecommons.org/licenses/by-sa/4.0/legalcode
  crypto.randomUUID = function randomUUID() {
    return (
      [1e7]+-1e3+-4e3+-8e3+-1e11).replace(/[018]/g,
      c => (c ^ crypto.getRandomValues(new Uint8Array(1))[0] & 15 >> c / 4).toString(16)
    );
  };


/* @ungap/url-search-params */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{!function(t,e){if(new t("q=%2B").get("q")!==e||new t({q:e}).get("q")!==e||new t([["q",e]]).get("q")!==e||"q=%0A"!==new t("q=\n").toString()||"q=+%26"!==new t({q:" &"}).toString()||"q=%25zx"!==new t({q:"%zx"}).toString())throw t;self.URLSearchParams=t}(URLSearchParams,"+")}catch(t){!function(t,a,o){"use strict";var u=t.create,h=t.defineProperty,e=/[!'\(\)~]|%20|%00/g,n=/%(?![0-9a-fA-F]{2})/g,r=/\+/g,i={"!":"%21","'":"%27","(":"%28",")":"%29","~":"%7E","%20":"+","%00":"\0"},s={append:function(t,e){p(this._ungap,t,e)},delete:function(t){delete this._ungap[t]},get:function(t){return this.has(t)?this._ungap[t][0]:null},getAll:function(t){return this.has(t)?this._ungap[t].slice(0):[]},has:function(t){return t in this._ungap},set:function(t,e){this._ungap[t]=[a(e)]},forEach:function(e,n){var r=this;for(var i in r._ungap)r._ungap[i].forEach(t,i);function t(t){e.call(n,t,a(i),r)}},toJSON:function(){return{}},toString:function(){var t=[];for(var e in this._ungap)for(var n=v(e),r=0,i=this._ungap[e];r<i.length;r++)t.push(n+"="+v(i[r]));return t.join("&")}};for(var c in s)h(f.prototype,c,{configurable:!0,writable:!0,value:s[c]});function f(t){var e=u(null);switch(h(this,"_ungap",{value:e}),!0){case!t:break;case"string"==typeof t:"?"===t.charAt(0)&&(t=t.slice(1));for(var n=t.split("&"),r=0,i=n.length;r<i;r++){var a=(s=n[r]).indexOf("=");-1<a?p(e,g(s.slice(0,a)),g(s.slice(a+1))):s.length&&p(e,g(s),"")}break;case o(t):for(var s,r=0,i=t.length;r<i;r++){p(e,(s=t[r])[0],s[1])}break;case"forEach"in t:t.forEach(l,e);break;default:for(var c in t)p(e,c,t[c])}}function l(t,e){p(this,e,t)}function p(t,e,n){var r=o(n)?n.join(","):n;e in t?t[e].push(r):t[e]=[r]}function g(t){return decodeURIComponent(t.replace(n,"%25").replace(r," "))}function v(t){return encodeURIComponent(t).replace(e,d)}function d(t){return i[t]}self.URLSearchParams=f}(Object,String,Array.isArray)}!function(d){var r=!1;try{r=!!Symbol.iterator}catch(t){}function t(t,e){var n=[];return t.forEach(e,n),r?n[Symbol.iterator]():{next:function(){var t=n.shift();return{done:void 0===t,value:t}}}}"forEach"in d||(d.forEach=function(n,r){var i=this,t=Object.create(null);this.toString().replace(/=[\s\S]*?(?:&|$)/g,"=").split("=").forEach(function(e){!e.length||e in t||(t[e]=i.getAll(e)).forEach(function(t){n.call(r,t,e,i)})})}),"keys"in d||(d.keys=function(){return t(this,function(t,e){this.push(e)})}),"values"in d||(d.values=function(){return t(this,function(t,e){this.push(t)})}),"entries"in d||(d.entries=function(){return t(this,function(t,e){this.push([e,t])})}),!r||Symbol.iterator in d||(d[Symbol.iterator]=d.entries),"sort"in d||(d.sort=function(){for(var t,e,n,r=this.entries(),i=r.next(),a=i.done,s=[],c=Object.create(null);!a;)e=(n=i.value)[0],s.push(e),e in c||(c[e]=[]),c[e].push(n[1]),a=(i=r.next()).done;for(s.sort(),t=0;t<s.length;t++)this.delete(s[t]);for(t=0;t<s.length;t++)e=s[t],this.append(e,c[e].shift())}),function(f){function l(t){var e=t.append;t.append=d.append,URLSearchParams.call(t,t._usp.search.slice(1)),t.append=e}function p(t,e){if(!(t instanceof e))throw new TypeError("'searchParams' accessed on an object that does not implement interface "+e.name)}function t(e){var n,r,i,t=e.prototype,a=v(t,"searchParams"),s=v(t,"href"),c=v(t,"search");function o(t,e){d.append.call(this,t,e),t=this.toString(),i.set.call(this._usp,t?"?"+t:"")}function u(t){d.delete.call(this,t),t=this.toString(),i.set.call(this._usp,t?"?"+t:"")}function h(t,e){d.set.call(this,t,e),t=this.toString(),i.set.call(this._usp,t?"?"+t:"")}!a&&c&&c.set&&(i=c,r=function(t,e){return t.append=o,t.delete=u,t.set=h,g(t,"_usp",{configurable:!0,writable:!0,value:e})},n=function(t,e){return g(t,"_searchParams",{configurable:!0,writable:!0,value:r(e,t)}),e},f.defineProperties(t,{href:{get:function(){return s.get.call(this)},set:function(t){var e=this._searchParams;s.set.call(this,t),e&&l(e)}},search:{get:function(){return c.get.call(this)},set:function(t){var e=this._searchParams;c.set.call(this,t),e&&l(e)}},searchParams:{get:function(){return p(this,e),this._searchParams||n(this,new URLSearchParams(this.search.slice(1)))},set:function(t){p(this,e),n(this,t)}}}))}var g=f.defineProperty,v=f.getOwnPropertyDescriptor;try{t(HTMLAnchorElement),/^function|object$/.test(typeof URL)&&URL.prototype&&t(URL)}catch(t){}}(Object)}(self.URLSearchParams.prototype,Object);

/* @ungap/weakmap */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.WeakMap=WeakMap}catch(t){self.WeakMap=function(e,t){"use strict";var n=t.defineProperty,a=t.hasOwnProperty,i=r.prototype;return i.delete=function(t){return this.has(t)&&delete t[this._]},i.get=function(t){return this.has(t)?t[this._]:void 0},i.has=function(t){return a.call(t,this._)},i.set=function(t,e){return n(t,this._,{configurable:!0,value:e}),this},r;function r(t){n(this,"_",{value:"_@ungap/weakmap"+e++}),t&&t.forEach(s,this)}function s(t){this.set(t[0],t[1])}}(Math.random(),Object)}

/* @ungap/weakset */
/*! (c) Andrea Giammarchi - ISC */
var self=this||{};try{self.WeakSet=WeakSet}catch(t){!function(e){var s=new e,t=n.prototype;function n(t){"use strict";s.set(this,new e),t&&t.forEach(this.add,this)}t.add=function(t){return s.get(this).set(t,1),this},t.delete=function(t){return s.get(this).delete(t)},t.has=function(t){return s.get(this).has(t)},self.WeakSet=n}(WeakMap)}

/* @ungap/trim-start */
var trimStart="".trimStart||function(){return this.replace(/^[\s\uFEFF\xA0]+/,"")};

/* @ungap/trim-end */
var trimEnd="".trimEnd||function(){return this.replace(/[\s\uFEFF\xA0]+$/,"")};
