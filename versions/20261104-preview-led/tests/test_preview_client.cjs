/* Tests execute the shipped JS policy, not a second implementation. */
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const { FramePolicy, validLights, MAX_FRAME_AGE_MS, REQUEST_TIMEOUT_MS } = require('../main/web/preview.js');
test('late JPEG still advances cursor, rejects duplicate and backward frame', () => {
  const p = new FramePolicy();
  assert.equal(p.receive('1234abcd', 9), true);
  assert.equal(p.remaining(200, 0, 150), 0);
  assert.equal(p.seq, 9);
  assert.equal(p.receive('1234abcd', 9), false);
  assert.equal(p.receive('1234abcd', 8), false);
  assert.equal(p.receive('1234abcd', 10), true);
});
test('display expiry includes age before drawing, does not add another 350ms', () => {
  const p = new FramePolicy(); p.drawn(120, 1000);
  assert.equal(p.expiresAt, 1180);
  assert.equal(p.expired(1179), false);
  assert.equal(p.expired(1180), true);
  assert.equal(p.remaining(120, 1000, 1180), 0);
  assert.equal(MAX_FRAME_AGE_MS, 300);
  assert.equal(REQUEST_TIMEOUT_MS, 600); // Request budget and frame freshness are distinct.
});
test('reboot resets cursor and rejects responses from retired boot; sequence wraps', () => {
  const p = new FramePolicy();
  assert.equal(p.receive('11111111', 0xffffffff), true);
  assert.equal(p.receive('11111111', 0), true);
  assert.equal(p.receive('11111111', 0xffffffff), false);
  assert.equal(p.receive('22222222', 1), true);
  assert.equal(p.receive('11111111', 2), false);
  assert.equal(p.boot, '22222222'); assert.equal(p.seq, 1);
});
test('invalid metadata and future request clocks never become fresh', () => {
  const p = new FramePolicy();
  for (const seq of [-1, NaN, Infinity, 1.5, 0x100000000]) assert.equal(p.receive('1234abcd', seq), false);
  for (const boot of ['', 'bad', null, '0000000g']) assert.equal(p.receive(boot, 1), false);
  for (const age of [-1, NaN, Infinity]) assert.equal(p.remaining(age, 0, 1), 0);
  assert.equal(p.remaining(1, 10, 0), 0);
});
const lights = () => ({ valid: true, age_ms: 5, epoch: 0, enabled: true,
  batch_active: true, selected: 0, four_color: 0,
  channels: [{ color: 'red', state: 2, multiplier: 4, on: true },
    { color: 'green', state: 1, multiplier: 2, on: true },
    { color: 'blue', state: 3, multiplier: 2, on: false }] });
test('lamp schema preserves off, pending, selected, used and rejects unknown/invalid', () => {
  assert.equal(validLights(lights()), true);
  for (const change of [x => x.valid = false, x => x.channels.pop(), x => x.age_ms = -1,
    x => x.channels[0].color = 'blue', x => x.channels[0].state = 7,
    x => x.channels[0].multiplier = 3, x => x.enabled = false]) {
    const x = lights(); change(x); assert.equal(validLights(x), false);
  }
  const zero = lights(); zero.channels = ['red','green','blue'].map(color => ({color,state:0,multiplier:0,on:false}));
  zero.enabled = false; zero.batch_active = false; zero.selected = -1;
  assert.equal(validLights(zero), true); zero.channels[0].on = true; assert.equal(validLights(zero), false);
});
