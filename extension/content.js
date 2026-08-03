'use strict';

function inspectControls() {
  return QuickSwapButtonFinder.collectControls(document)
    .map((element) => ({
      label: QuickSwapButtonFinder.controlLabel(element),
      tag: element.tagName,
      role: element.getAttribute('role'),
      ariaSelected: element.getAttribute('aria-selected'),
      ariaPressed: element.getAttribute('aria-pressed'),
      dataState: element.getAttribute('data-state'),
      disabled: Boolean(element.disabled) || element.getAttribute('aria-disabled') === 'true',
    }))
    .filter((control) => /auction|giveaway|start|run/.test(control.label))
    .slice(0, 30);
}

function isHitTarget(element) {
  if (typeof document.elementsFromPoint !== 'function') return true;
  const rect = element.getBoundingClientRect();
  if (rect.width <= 0 || rect.height <= 0) return false;
  const stack = document.elementsFromPoint(
    rect.left + rect.width / 2,
    rect.top + rect.height / 2
  );
  return stack.some((candidate) => candidate === element || element.contains(candidate));
}

function sleep(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

async function waitForReadyPlan(action, previousGeneric, timeoutMs = 2000) {
  const deadline = Date.now() + timeoutMs;
  let selectedAt = null;
  do {
    const controls = QuickSwapButtonFinder.collectControls(document);
    const plan = QuickSwapActionLogic.planAction(action, controls, QuickSwapButtonFinder);
    if (plan.modeSelected && selectedAt === null) selectedAt = Date.now();
    const selectedForMs = selectedAt === null ? 0 : Date.now() - selectedAt;
    if (QuickSwapActionLogic.isReadyAfterModeChange(plan, previousGeneric, selectedForMs)) {
      return plan;
    }
    await sleep(50);
  } while (Date.now() < deadline);
  return null;
}

async function runAction(action) {
  if (action === 'status') {
    return {
      ok: true,
      message: `Whatnot page ready: ${location.pathname}`,
      url: location.href,
    };
  }

  if (action === 'inspect') {
    const controls = inspectControls();
    return {
      ok: true,
      message: `${controls.length} candidate control label(s) found`,
      controls,
      url: location.href,
    };
  }

  if (action !== 'auction' && action !== 'giveaway') {
    return { ok: false, error: `Unsupported page action: ${action}` };
  }

  const controls = QuickSwapButtonFinder.collectControls(document);
  let plan = QuickSwapActionLogic.planAction(action, controls, QuickSwapButtonFinder);
  if (!plan.mode) {
    return {
      ok: false,
      error: `No unique enabled ${action} mode control was found`,
      controls: inspectControls(),
    };
  }

  if (!plan.modeSelected) {
    const previousGeneric = QuickSwapButtonFinder.chooseUnique(['run next'], controls);
    if (!isHitTarget(plan.mode.element)) {
      return { ok: false, error: `The ${plan.mode.label} mode control is covered or off-screen` };
    }
    plan.mode.element.click();
    plan = await waitForReadyPlan(action, previousGeneric);
  }

  if (!plan || !plan.trigger || !isHitTarget(plan.trigger.element)) {
    return {
      ok: false,
      error: `No unique visible start control became ready for ${action}`,
      controls: inspectControls(),
    };
  }

  plan.trigger.element.click();
  return {
    ok: true,
    message: `Selected ${plan.mode.label} and triggered ${plan.trigger.label}`,
    label: plan.trigger.label,
    url: location.href,
  };
}

const enqueueCommand = QuickSwapActionLogic.createCommandQueue(runAction);

browser.runtime.onMessage.addListener((message) => {
  if (!message || message.type !== 'quick-swap-command') return undefined;
  const id = message.id || `${message.action}:${Date.now()}`;
  return enqueueCommand(id, message.action);
});
