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
  function parsePacketHeader(buffer, totalSize) {
    if (!(buffer instanceof ArrayBuffer) || buffer.byteLength !== 32) throw new Error('Invalid header size');
    const v = new DataView(buffer);
    if (v.getUint32(0) !== 0x524d4350 || v.getUint8(4) !== 1 || v.getUint8(5) !== 0 || v.getUint16(6) !== 32)
      throw new Error('Invalid frame version');
    const length = v.getUint32(24), token = v.getUint32(16);
    if (!token || !length || length > 262144 || totalSize !== 32 + length || v.getUint16(28) !== 640 || v.getUint16(30) !== 480)
      throw new Error('Invalid frame metadata');
    return { boot: v.getUint32(8).toString(16).padStart(8, '0'), sequence: v.getUint32(12),
      token, age: v.getUint32(20), length };
  }
  const api = { FramePolicy, validLights, parsePacketHeader, MAX_FRAME_AGE_MS, REQUEST_TIMEOUT_MS, LIGHT_STALE_MS };
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
  let streamPort = 0;
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
        rates = '输入 ' + fps('received') + ' · ' + (data.capture_format === 'MJPEG' ? '直传 ' : '编码 ') + fps('encoded') + ' · 发出 ' + fps('sent') + ' 帧/秒';
      }
      diagnosticText.textContent = data.capture_format + ' · 协商请求 ' + data.capture_fps_config + ' FPS\n' + rates +
          (data.capture_format === 'MJPEG' ? '\n最近识别解码 ' + data.last_decode_ms : '\n最近编码 ' + data.last_encode_ms) + ' ms · 最近发送 ' + data.last_send_ms +
        ' ms\n丢弃 ' + data.dropped +
        (data.capture_format === 'MJPEG' ? ' · JPEG准备错误 ' + data.mjpeg_prepare_errors + ' · 解码错误 ' + data.mjpeg_decode_errors : ' · 编码错误 ' + data.encode_errors) +
        ' · 发送错误 ' + data.send_errors +
        '\n自适应目标 ' + data.stream_target_fps + ' FPS · 回执 ' + data.last_ack_ms + ' ms' +
        '\n连接恢复尝试 ' + data.net_attempts + (data.net_exhausted ? ' · 需要检查网络模块' : '') +
        '\n发送与回执耗时不等于完整拍摄延迟。';
    }
    previousStats = { data, now };
  }
  function receive(mine) {
    if (!current(mine)) return;
    if (!streamPort) { setTimeout(() => { if (current(mine) && !controller) receive(mine); }, 50); return; }
    if (typeof WebSocket === 'undefined' || typeof createImageBitmap !== 'function') {
      message('浏览器不支持', '当前浏览器无法显示图传', '请使用新版 Chrome 或 Edge', true); return;
    }
    let socket;
    const retry = () => {
      failures = Math.min(failures + 1, 5);
      setTimeout(() => { if (current(mine) && !controller) receive(mine); }, Math.min(200 * 2 ** (failures - 1), 2500));
    };
    try {
      const url = new URL('/stream', location.href); url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
      url.port = String(streamPort); socket = new WebSocket(url.href); socket.binaryType = 'blob';
    } catch (error) { retry(); return; }
    let closed = false, canceled = false, busy = false, tokenCounter = 0, pendingToken = 0, pendingStarted = 0, timeout;
    const session = { abort() { canceled = closed = true; clearTimeout(timeout); socket.close(); } };
    controller = session;
    const valid = () => !closed && current(mine) && controller === session;
    const credit = (ack, shown) => {
      if (!valid() || socket.readyState !== WebSocket.OPEN) return;
      tokenCounter = (tokenCounter + 1) >>> 0; if (!tokenCounter) tokenCounter = 1;
      pendingToken = tokenCounter; pendingStarted = performance.now();
      socket.send('N ' + pendingToken + ' ' + ack + ' ' + (shown ? 1 : 0));
      timeout = setTimeout(() => { if (valid()) socket.close(1000, 'Frame timeout'); }, REQUEST_TIMEOUT_MS);
    };
    socket.onopen = () => { if (valid()) credit(0, true); else socket.close(); };
    socket.onmessage = async event => {
      if (!valid()) return;
      if (busy || !pendingToken) { socket.close(1002, 'Unexpected frame'); return; }
      busy = true; clearTimeout(timeout);
      const expected = pendingToken, started = pendingStarted; pendingToken = 0;
      let bitmap = null, shown = false, protocolValid = false;
      try {
        if (!(event.data instanceof Blob)) throw new Error('Invalid packet');
        const header = parsePacketHeader(await event.data.slice(0, 32).arrayBuffer(), event.data.size);
        if (!valid()) return;
        if (header.token !== expected) throw new Error('Credit mismatch');
        protocolValid = true;
        const before = policy.boot, eligible = policy.receive(header.boot, header.sequence); bootChanged(before);
        if (!eligible || policy.remaining(header.age, started, performance.now()) <= 0) return;
        bitmap = await createImageBitmap(event.data.slice(32));
        if (!valid() || header.boot !== policy.boot || policy.remaining(header.age, started, performance.now()) <= 0) return;
        const now = performance.now();
        ctx.drawImage(bitmap, 0, 0, canvas.width, canvas.height); policy.drawn(header.age, started);
        lastDraw = now; count++; shown = true; failures = 0;
        state.textContent = '实时画面'; state.dataset.live = 'true'; camera.dataset.stale = 'false'; notice.hidden = true;
        document.querySelector('#age').textContent = Math.ceil(header.age + now - started) + ' ms';
      } catch (error) {
        if (valid() && !protocolValid) socket.close(1002, 'Invalid frame');
        else if (valid() && !lastDraw) message('等待画面', '画面暂不可用', '正在等待可显示的新画面', true);
      } finally {
        if (bitmap) bitmap.close(); busy = false;
        if (protocolValid && valid()) credit(expected, shown);
      }
    };
    socket.onerror = () => { if (!closed) socket.close(); };
    socket.onclose = () => {
      closed = true; clearTimeout(timeout);
      if (controller === session) controller = null;
      if (!canceled && current(mine)) {
        policy.expiresAt = 0; lastDraw = 0; count = 0; rateStart = performance.now();
        document.querySelector('#age').textContent = '—';
        message('正在重连', '图传连接中断', '确认手机仍连接热点，正在重新获取画面', true); retry();
      }
    };
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
        if (Number.isInteger(data.stream_port) && data.stream_port > 0 && data.stream_port <= 65535 && streamPort !== data.stream_port) {
          streamPort = data.stream_port; generation++; if (controller) controller.abort(); controller = null;
          if (active && !document.hidden) receive(generation);
        }
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
