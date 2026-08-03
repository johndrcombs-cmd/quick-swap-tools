const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const finder = require(path.join(__dirname, '..', 'extension', 'button_finder.js'));

function control({ text = '', aria = '', disabled = false, role = 'button', visible = true, style = {} } = {}) {
  const element = {
    textContent: text,
    disabled,
    hidden: !visible,
    getAttribute(name) {
      if (name === 'aria-label') return aria || null;
      if (name === 'aria-disabled') return disabled ? 'true' : null;
      if (name === 'role') return role;
      return null;
    },
    matches(selector) {
      return selector.includes('button') || (role === 'button' && selector.includes('[role="button"]'));
    },
    getClientRects() {
      return visible ? [{}] : [];
    },
  };
  element.ownerDocument = {
    defaultView: {
      getComputedStyle() {
        return {
          display: style.display || 'block',
          visibility: style.visibility || 'visible',
          opacity: style.opacity ?? '1',
          pointerEvents: style.pointerEvents || 'auto',
        };
      },
    },
  };
  return element;
}

test('finds the exact enabled next-auction control', () => {
  const expected = control({ text: 'Start next auction' });
  const result = finder.chooseControl('auction', [
    control({ text: 'Add auction' }),
    expected,
    control({ text: 'Start next giveaway' }),
  ]);
  assert.equal(result.element, expected);
  assert.equal(result.label, 'start next auction');
});

test('finds an aria-labelled giveaway control', () => {
  const expected = control({ aria: 'Start next giveaway' });
  assert.equal(finder.chooseControl('giveaway', [expected]).element, expected);
});

test('rejects disabled and hidden controls', () => {
  assert.equal(finder.chooseControl('auction', [
    control({ text: 'Start next auction', disabled: true }),
    control({ text: 'Start next auction', visible: false }),
  ]), null);
});

test('does not guess from an unrelated generic start button', () => {
  assert.equal(finder.chooseControl('auction', [
    control({ text: 'Start' }),
    control({ text: 'Start stream' }),
    control({ text: 'Add auction' }),
  ]), null);
});

test('rejects two equally strong matches to prevent double starts', () => {
  assert.equal(finder.chooseControl('giveaway', [
    control({ text: 'Start next giveaway' }),
    control({ text: 'Start next giveaway' }),
  ]), null);
});

test('rejects multiple accepted controls even when labels differ', () => {
  assert.equal(finder.chooseControl('auction', [
    control({ text: 'Start next auction' }),
    control({ text: 'Start auction' }),
  ]), null);
});

test('chooses the unique Whatnot Run next control', () => {
  const expected = control({ text: 'Run next' });
  assert.equal(finder.chooseUnique(['run next'], [
    control({ text: 'Auction' }),
    expected,
    control({ text: 'Giveaways' }),
  ]).element, expected);
});

test('rejects CSS-hidden, transparent, and pointer-disabled controls', () => {
  for (const style of [
    { visibility: 'hidden' },
    { opacity: '0' },
    { pointerEvents: 'none' },
    { display: 'none' },
  ]) {
    assert.equal(finder.chooseUnique(['run next'], [
      control({ text: 'Run next', style }),
    ]), null);
  }
});
