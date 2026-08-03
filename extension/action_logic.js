(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.QuickSwapActionLogic = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  function modeLabels(action) {
    return action === 'auction' ? ['auction', 'auctions'] : ['giveaway', 'giveaways'];
  }

  function isModeSelected(element) {
    return element.getAttribute('aria-selected') === 'true' ||
      element.getAttribute('aria-pressed') === 'true' ||
      element.getAttribute('data-state') === 'active' ||
      element.getAttribute('data-selected') === 'true';
  }

  function planAction(action, controls, finder) {
    const mode = finder.chooseUnique(modeLabels(action), controls);
    if (!mode) return { mode: null, modeSelected: false, trigger: null };
    const modeSelected = isModeSelected(mode.element);
    if (!modeSelected) return { mode, modeSelected, trigger: null };
    const trigger = finder.chooseControl(action, controls) ||
      finder.chooseUnique(['run next'], controls);
    return { mode, modeSelected, trigger };
  }

  function isReadyAfterModeChange(plan, previousGeneric, selectedForMs) {
    if (!plan || !plan.modeSelected || !plan.trigger) return false;
    if (plan.trigger.label !== 'run next') return true;
    if (!previousGeneric || plan.trigger.element !== previousGeneric.element) return true;
    return selectedForMs >= 300;
  }

  function createCommandQueue(executor, retentionMs = 60000) {
    let tail = Promise.resolve();
    const commands = new Map();
    return function enqueue(id, action) {
      if (commands.has(id)) return commands.get(id);
      const promise = tail.catch(() => undefined).then(() => executor(action, id));
      tail = promise;
      commands.set(id, promise);
      setTimeout(() => commands.delete(id), retentionMs);
      return promise;
    };
  }

  return { createCommandQueue, isModeSelected, isReadyAfterModeChange, modeLabels, planAction };
});
