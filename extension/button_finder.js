(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.QuickSwapButtonFinder = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const LABEL_SCORES = Object.freeze({
    auction: new Map([
      ['start next auction', 100],
      ['run next auction', 95],
      ['start auction', 80],
    ]),
    giveaway: new Map([
      ['start next giveaway', 100],
      ['run next giveaway', 95],
      ['start giveaway', 80],
    ]),
  });

  function normalizeLabel(value) {
    return String(value || '')
      .replace(/\s+/g, ' ')
      .trim()
      .toLowerCase();
  }

  function controlLabel(element) {
    return normalizeLabel(
      element.getAttribute('aria-label') ||
      element.getAttribute('title') ||
      element.textContent
    );
  }

  function isUsable(element) {
    if (element.disabled ||
        element.getAttribute('aria-disabled') === 'true' ||
        element.hidden ||
        element.getClientRects().length === 0) {
      return false;
    }
    const view = element.ownerDocument && element.ownerDocument.defaultView;
    if (view && typeof view.getComputedStyle === 'function') {
      for (let current = element; current; current = current.parentElement) {
        const style = view.getComputedStyle(current);
        if (style.display === 'none' ||
            style.visibility === 'hidden' ||
            style.visibility === 'collapse' ||
            Number(style.opacity) <= 0.05 ||
            style.pointerEvents === 'none') {
          return false;
        }
      }
    }
    return true;
  }

  function chooseUnique(labels, elements) {
    const accepted = new Set(labels.map(normalizeLabel));
    const matches = [];
    for (const element of elements) {
      if (!isUsable(element)) continue;
      const label = controlLabel(element);
      if (accepted.has(label)) matches.push({ element, label });
    }
    return matches.length === 1 ? matches[0] : null;
  }

  function chooseControl(action, elements) {
    const scores = LABEL_SCORES[action];
    if (!scores) return null;
    const match = chooseUnique([...scores.keys()], elements);
    return match ? { ...match, score: scores.get(match.label) } : null;
  }

  function collectControls(rootNode) {
    const found = [];
    const visit = (node) => {
      if (!node || typeof node.querySelectorAll !== 'function') return;
      for (const element of node.querySelectorAll('button,[role="button"],input[type="button"],input[type="submit"]')) {
        found.push(element);
        if (element.shadowRoot) visit(element.shadowRoot);
      }
      for (const element of node.querySelectorAll('*')) {
        if (element.shadowRoot) visit(element.shadowRoot);
      }
    };
    visit(rootNode);
    return [...new Set(found)];
  }

  return { chooseControl, chooseUnique, collectControls, controlLabel, isUsable, normalizeLabel };
});
