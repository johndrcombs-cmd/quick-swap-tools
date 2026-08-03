'use strict';

const NATIVE_HOST = 'com.onibyts.quickswap';
const RECONNECT_DELAY_MS = 1500;
let nativePort = null;
let reconnectTimer = null;

async function whatnotTabs() {
  return browser.tabs.query({ url: 'https://*.whatnot.com/*' });
}

async function handleCommand(message) {
  const base = {
    type: 'result',
    id: message && message.id,
    action: message && message.action,
  };

  if (!message || message.type !== 'command') {
    return { ...base, ok: false, error: 'Invalid native command' };
  }

  const tabs = await whatnotTabs();
  if (message.action === 'status') {
    return {
      ...base,
      ok: true,
      message: `Bridge ready; ${tabs.length} Whatnot tab(s) detected`,
      tabs: tabs.length,
    };
  }

  const tab = QuickSwapRouting.chooseTab(tabs);
  if (!tab || typeof tab.id !== 'number') {
    return { ...base, ok: false, error: 'No Whatnot tab is open in Firefox' };
  }

  try {
    const result = await browser.tabs.sendMessage(tab.id, {
      type: 'quick-swap-command',
      id: message.id,
      action: message.action,
    });
    return { ...base, ...result, tabId: tab.id };
  } catch (error) {
    return {
      ...base,
      ok: false,
      error: `Whatnot page is not ready: ${error && error.message ? error.message : error}`,
    };
  }
}

function scheduleReconnect() {
  if (reconnectTimer !== null) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connectNativeHost();
  }, RECONNECT_DELAY_MS);
}

function connectNativeHost() {
  if (nativePort !== null) return;
  try {
    const port = browser.runtime.connectNative(NATIVE_HOST);
    nativePort = port;
    port.onMessage.addListener((message) => {
      if (!message || message.type !== 'command') return;
      handleCommand(message)
        .then((result) => port.postMessage(result))
        .catch((error) => port.postMessage({
          type: 'result',
          id: message.id,
          action: message.action,
          ok: false,
          error: String(error),
        }));
    });
    port.onDisconnect.addListener(() => {
      if (nativePort === port) nativePort = null;
      scheduleReconnect();
    });
    port.postMessage({ type: 'hello', version: browser.runtime.getManifest().version });
  } catch (_error) {
    nativePort = null;
    scheduleReconnect();
  }
}

browser.runtime.onStartup.addListener(connectNativeHost);
browser.runtime.onInstalled.addListener(connectNativeHost);
browser.browserAction.onClicked.addListener(async () => {
  const result = await handleCommand({ type: 'command', id: 'toolbar', action: 'status' });
  console.info('Quick Swap Tools:', result.message || result.error);
});
connectNativeHost();
