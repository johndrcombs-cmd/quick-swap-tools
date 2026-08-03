const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const finder = require(path.join(__dirname, '..', 'extension', 'button_finder.js'));
const logic = require(path.join(__dirname, '..', 'extension', 'action_logic.js'));

function control(label, { selected = null, disabled = false } = {}) {
  const element = {
    textContent: label,
    disabled,
    hidden: false,
    parentElement: null,
    getAttribute(name) {
      if (name === 'aria-selected') return selected === null ? null : String(selected);
      if (name === 'aria-disabled') return disabled ? 'true' : null;
      return null;
    },
    getClientRects() { return [{}]; },
  };
  element.ownerDocument = {
    defaultView: {
      getComputedStyle() {
        return { display: 'block', visibility: 'visible', opacity: '1', pointerEvents: 'auto' };
      },
    },
  };
  return element;
}

test('requires selecting Giveaways before choosing a start control', () => {
  const controls = [control('Auction', { selected: true }), control('Giveaways', { selected: false }), control('Run next')];
  const plan = logic.planAction('giveaway', controls, finder);
  assert.equal(plan.mode.label, 'giveaways');
  assert.equal(plan.modeSelected, false);
  assert.equal(plan.trigger, null);
});

test('uses Start giveaway after Giveaways finishes rendering', () => {
  const expected = control('Start giveaway');
  const controls = [control('Auction', { selected: false }), control('Giveaways', { selected: true }), expected];
  const plan = logic.planAction('giveaway', controls, finder);
  assert.equal(plan.modeSelected, true);
  assert.equal(plan.trigger.element, expected);
  assert.equal(plan.trigger.label, 'start giveaway');
});

test('uses Run next when the selected auction mode exposes that control', () => {
  const expected = control('Run next');
  const controls = [control('Auction', { selected: true }), control('Giveaways', { selected: false }), expected];
  const plan = logic.planAction('auction', controls, finder);
  assert.equal(plan.modeSelected, true);
  assert.equal(plan.trigger.element, expected);
});

test('waits for a reused generic Run next control to belong to the newly selected mode', () => {
  const reused = control('Run next');
  const plan = {
    mode: { element: control('Auction', { selected: true }), label: 'auction' },
    modeSelected: true,
    trigger: { element: reused, label: 'run next' },
  };
  assert.equal(logic.isReadyAfterModeChange(plan, { element: reused }, 100), false);
  assert.equal(logic.isReadyAfterModeChange(plan, { element: reused }, 300), true);
});

test('queues distinct rapid commands but deduplicates the same command id', async () => {
  const calls = [];
  const enqueue = logic.createCommandQueue(async (action, id) => {
    calls.push(`${action}:${id}`);
    return action;
  }, 0);

  const first = enqueue('id-1', 'auction');
  const duplicate = enqueue('id-1', 'auction');
  const second = enqueue('id-2', 'giveaway');
  assert.equal(await first, 'auction');
  assert.equal(await duplicate, 'auction');
  assert.equal(await second, 'giveaway');
  assert.deepEqual(calls, ['auction:id-1', 'giveaway:id-2']);
});
