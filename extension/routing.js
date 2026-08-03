(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.QuickSwapRouting = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  function tabScore(tab) {
    const url = String(tab.url || '').toLowerCase();
    let score = 0;
    if (/whatnot\.com\/(live|seller\/live|dashboard\/live)/.test(url)) score += 100;
    if (tab.active) score += 50;
    if (tab.audible) score += 30;
    if (/whatnot\.com\/(dashboard|seller)/.test(url)) score += 20;
    return score;
  }

  function chooseTab(tabs) {
    if (!Array.isArray(tabs) || tabs.length === 0) return null;
    return [...tabs].sort((left, right) => tabScore(right) - tabScore(left))[0] || null;
  }

  return { chooseTab, tabScore };
});
