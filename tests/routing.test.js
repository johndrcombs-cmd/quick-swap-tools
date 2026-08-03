const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const routing = require(path.join(__dirname, '..', 'extension', 'routing.js'));

test('prefers the active live Whatnot tab', () => {
  const tabs = [
    { id: 1, active: false, url: 'https://www.whatnot.com/dashboard' },
    { id: 2, active: true, url: 'https://www.whatnot.com/live/abc' },
    { id: 3, active: false, audible: true, url: 'https://www.whatnot.com/live/xyz' },
  ];
  assert.equal(routing.chooseTab(tabs).id, 2);
});

test('prefers an audible live tab when Firefox is not focused', () => {
  const tabs = [
    { id: 1, active: true, url: 'https://www.whatnot.com/dashboard' },
    { id: 2, active: false, audible: true, url: 'https://www.whatnot.com/live/abc' },
  ];
  assert.equal(routing.chooseTab(tabs).id, 2);
});

test('returns null when there are no Whatnot tabs', () => {
  assert.equal(routing.chooseTab([]), null);
});
