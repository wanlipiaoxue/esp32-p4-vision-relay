/* SPDX-License-Identifier: Apache-2.0 */
(function (root) {
  'use strict';
  const MAX_FRAME_AGE_MS = 300, REQUEST_TIMEOUT_MS = 600, LIGHT_STALE_MS = 1000;
  const COLORS = ['red', 'green', 'blue'];
  class FramePolicy {
    constructor() { this.boot = ''; this.seq = -1; this.expiresAt = 0; this.retired = new Set(); }
    adoptBoot(value) {
      if (typeof value !== 'string' || !/^[0-9a-f]{8}$/.test(value) || this.retired.has(value)) return false;
      if (this.boot !== value) {
        if (this.boot) this.retired.add(this.boot);
        if (this.retired.size > 4) this.retired.delete(this.retired.values().next().value);
        this.boot = value; this.seq = -1; this.expiresAt = 0;
      }
      return true;
    }
    receive(boot, sequence) {
      if (!Number.isInteger(sequence) || sequence < 0 || sequence > 0xffffffff || !this.adoptBoot(boot)) return false;
      const distance = (sequence - this.seq) >>> 0;
      if (this.seq >= 0 && (distance === 0 || distance >= 0x80000000)) return false;
      this.seq = sequence; // Advance even if this JPEG later arrives too late or fails to decode.
      return true;
    }
    remaining(deviceAge, started, now) {
      if (!Number.isFinite(deviceAge) || deviceAge < 0 || !Number.isFinite(started) || now < started) return 0;
      return Math.max(0, MAX_FRAME_AGE_MS - deviceAge - (now - started));
    }
    drawn(deviceAge, started) { this.expiresAt = started + MAX_FRAME_AGE_MS - deviceAge; }
    expired(now) { return this.expiresAt > 0 && now >= this.expiresAt; }
  }
  function validLights(lights) {
    return !!lights && lights.valid === true && Number.isInteger(lights.age_ms) && lights.age_ms >= 0 &&
      Number.isInteger(lights.epoch) && lights.epoch >= 0 && lights.epoch <= 0xffffffff &&
      typeof lights.enabled === 'boolean' && typeof lights.batch_active === 'boolean' &&
      Number.isInteger(lights.selected) && lights.selected >= -1 && lights.selected < 3 &&
      Number.isInteger(lights.four_color) && lights.four_color >= 0 && lights.four_color < 3 &&
      Array.isArray(lights.channels) && lights.channels.length === 3 && lights.channels.every((item, i) =>
        item && item.color === COLORS[i] && Number.isInteger(item.state) && item.state >= 0 && item.state <= 3 &&
        typeof item.on === 'boolean' && (item.state === 0 ? item.multiplier === 0 && !item.on :
          item.multiplier === 2 || item.multiplier === 4) && (lights.enabled || !item.on));
  }
  const api = { FramePolicy, validLights, MAX_FRAME_AGE_MS, REQUEST_TIMEOUT_MS, LIGHT_STALE_MS };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.RMPreview = api;
  if (typeof document === 'undefined') return;

  const camera = document.querySelector('#camera'), canvas = document.querySelector('#view');
  const ctx = canvas.getContext('2d'), state = document.querySelector('#state');
  const notice = document.querySelector('#notice'), title = document.querySelector('#noticeTitle');
  const text = document.querySelector('#noticeText'), pause = document.querySelector('#pause');
  const lampPanel = document.querySelector('#lamps'), lampMeta = document.querySelector('#lampMeta');
  const diagnostics = document.querySelector('#diagnosticPanel'), diagnosticText = document.querySelector('#diagnosticText');
  const policy = new FramePolicy(), sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  let active = true, generation = 0, statusGeneration = 0, controller = null, statusController = null;
  let lastDraw = 0, count = 0, rateStart = performance.now(), failures = 0, statusExpires = 0, previousStats = null;
  const current = mine => active && mine === generation && !document.hidden;
  function message(label, heading, detail, clear = false) {
    state.textContent = label; state.dataset.live = 'false'; camera.dataset.stale = 'true';
    notice.hidden = false; title.textContent = heading; text.textContent = detail;
    if (clear) ctx.clearRect(0, 0, canvas.width, canvas.height);
  }
  function unknownLights(label = '等待灯控状态') {
    lampPanel.dataset.fresh = 'false'; lampMeta.textContent = label;
    for (const color of COLORS) {
      const item = document.querySelector('#lamp-' + color);
      item.dataset.state = 'unknown'; item.querySelector('.lamp-state').textContent = '未知';
      item.querySelector('.multiplier').textContent = '—';
    }
  }
  function bootChanged(previous) {
    if (previous && previous !== policy.boot) {
      previousStats = null; statusExpires = 0; unknownLights('板卡已重启，正在同步');
      lastDraw = 0;
      if (active) message('正在连接', '板卡已重启', '正在等待新画面', true);
    }
  }
  function applyLights(data, started, now) {
    const lights = data.lights;
    if (!validLights(lights)) return false;
    const remaining = LIGHT_STALE_MS - lights.age_ms - (now - started);
    if (remaining <= 0) return false;
    const before = policy.boot;
    if (!policy.adoptBoot(data.boot_id)) return false;
    bootChanged(before); statusExpires = now + remaining;
    lampPanel.dataset.fresh = 'true'; lampMeta.textContent = lights.enabled ? '已同步' : '识别已暂停';
    const labels = ['未识别', '常亮 · 待用', '快闪 · 本批', '慢闪 · 已用'];
    lights.channels.forEach((value, i) => {
      const item = document.querySelector('#lamp-' + COLORS[i]);
      item.dataset.state = String(value.state); item.dataset.sampleOn = String(value.on);
      item.querySelector('.lamp-state').textContent = labels[value.state];
      item.querySelector('.multiplier').textContent = value.state ? value.multiplier + '倍' : '—';
    });
    return true;
  }
  function showDiagnostics(data, now) {
    if (diagnostics.open) {
      let rates = '正在统计连接帧率…';
      if (previousStats && previousStats.data.boot_id === data.boot_id) {
        const seconds = (now - previousStats.now) / 1000;
        const fps = key => Number.isFinite(data[key]) && Number.isFinite(previousStats.data[key]) && seconds > 0 ?
          Math.max(0, (data[key] - previousStats.data[key]) / seconds).toFixed(1) : '—';
        rates = '输入 ' + fps('received') + ' · 编码 ' + fps('encoded') + ' · 发出 ' + fps('sent') + ' 帧/秒';
      }
      diagnosticText.textContent = rates + '\n最近编码 ' + data.last_encode_ms + ' ms · 最近发送 ' + data.last_send_ms +
        ' ms\n丢弃 ' + data.dropped + ' · 编码错误 ' + data.encode_errors + ' · 发送错误 ' + data.send_errors +
        '\n发送耗时不等于手机接收延迟。';
    }
    previousStats = { data, now };
  }
  async function receive(mine) {
    while (current(mine)) {
      const request = new AbortController(); controller = request;
      const started = performance.now(), timeout = setTimeout(() => request.abort(), REQUEST_TIMEOUT_MS);
      let bitmap = null;
      try {
        const response = await fetch('/frame.jpg?after=' + policy.seq + '&boot=' + encodeURIComponent(policy.boot),
          { cache: 'no-store', signal: request.signal });
        if (!current(mine)) break;
        if (response.status === 204) { failures = 0; await sleep(25); continue; }
        if (response.status === 503) {
          if (!lastDraw) message('等待图传', '图传暂不可用', '正在等待板卡与摄像头的最新画面');
          await sleep(100); continue;
        }
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const rawSeq = response.headers.get('X-Frame-Seq'), rawAge = response.headers.get('X-Frame-Age-Ms');
        const incomingBoot = response.headers.get('X-Boot-Id');
        if (rawSeq === null || rawAge === null || !/^\d+$/.test(rawSeq) || !/^\d+$/.test(rawAge)) throw new Error('Invalid frame metadata');
        const sequence = Number(rawSeq), age = Number(rawAge), before = policy.boot;
        const eligible = Number.isSafeInteger(age) && policy.receive(incomingBoot, sequence);
        bootChanged(before);
        const blob = await response.blob(); clearTimeout(timeout);
        if (!current(mine) || !eligible || incomingBoot !== policy.boot) continue;
        failures = 0;
        if (policy.remaining(age, started, performance.now()) <= 0) continue;
        bitmap = await createImageBitmap(blob);
        if (!current(mine) || incomingBoot !== policy.boot || policy.remaining(age, started, performance.now()) <= 0) continue;
        const now = performance.now(), ageUpper = age + now - started;
        ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height); policy.drawn(age, started);
        lastDraw = now; count++;
        state.textContent = '实时画面'; state.dataset.live = 'true'; camera.dataset.stale = 'false'; notice.hidden = true;
        document.querySelector('#age').textContent = Math.ceil(ageUpper) + ' ms';
      } catch (error) {
        if (!current(mine)) break;
        if (!lastDraw) message('正在重连', '连接中断', '确认手机仍连接 RM-P4-Camera 热点');
        failures = Math.min(failures + 1, 4); await sleep(Math.min(20 * 2 ** (failures - 1), 160));
      } finally {
        clearTimeout(timeout); if (controller === request) controller = null; if (bitmap) bitmap.close();
      }
    }
  }
  async function receiveStatus(mine) {
    while (mine === statusGeneration && !document.hidden) {
      const request = new AbortController(); statusController = request;
      const started = performance.now(), timeout = setTimeout(() => request.abort(), REQUEST_TIMEOUT_MS);
      try {
        const response = await fetch('/status', { cache: 'no-store', signal: request.signal });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const data = await response.json(), now = performance.now();
        if (mine !== statusGeneration || document.hidden) break;
        if (!applyLights(data, started, now) && !statusExpires) unknownLights();
        showDiagnostics(data, now);
      } catch (error) {
        if (mine !== statusGeneration || document.hidden) break;
        if (!statusExpires) unknownLights('灯控暂未连接');
      } finally {
        clearTimeout(timeout); if (statusController === request) statusController = null;
      }
      await sleep(250); // One in-flight request. Continues while the picture is paused.
    }
  }
  setInterval(() => {
    const now = performance.now();
    if (now - rateStart >= 1000) {
      document.querySelector('#fps').textContent = (count * 1000 / (now - rateStart)).toFixed(1);
      count = 0; rateStart = now;
    }
    if (active && !document.hidden && policy.expired(now) && camera.dataset.stale !== 'true') {
      message('画面已过期', '画面已过期', '正在等待最新画面，请勿按旧画面判断夹持状态', true);
      document.querySelector('#age').textContent = '—';
    }
    if (statusExpires && now >= statusExpires) { statusExpires = 0; unknownLights('灯控状态已过期'); }
  }, 50);
  pause.addEventListener('click', () => {
    active = !active; generation++; if (controller) controller.abort();
    pause.textContent = active ? '暂停画面' : '继续画面'; pause.setAttribute('aria-pressed', String(!active));
    if (active) { policy.expiresAt = 0; lastDraw = 0; message('正在连接', '正在恢复画面', '只显示收到的最新帧', true); receive(generation); }
    else message('已暂停', '画面已暂停', '板端识别与灯控状态继续更新', true);
  });
  document.querySelector('#fullscreen').addEventListener('click', async () => {
    try { if (document.fullscreenElement) await document.exitFullscreen(); else await document.documentElement.requestFullscreen(); }
    catch (error) { document.querySelector('#hint').textContent = '浏览器未允许全屏，可以横屏查看。'; }
  });
  diagnostics.addEventListener('toggle', () => { previousStats = null; });
  document.addEventListener('visibilitychange', () => {
    generation++; statusGeneration++; if (controller) controller.abort(); if (statusController) statusController.abort();
    if (document.hidden) {
      policy.expiresAt = 0; statusExpires = 0; unknownLights('页面已切到后台');
      message('已暂停', '页面已切到后台', '返回后重新获取画面与灯控状态', true);
    } else {
      statusExpires = 0; lastDraw = 0; policy.expiresAt = 0; previousStats = null;
      receiveStatus(statusGeneration);
      if (active) { message('正在连接', '正在恢复画面', '只显示收到的最新帧', true); receive(generation); }
      else message('已暂停', '画面已暂停', '板端识别与灯控状态继续更新', true);
    }
  });
  unknownLights(); receive(generation); receiveStatus(statusGeneration);
})(globalThis);
