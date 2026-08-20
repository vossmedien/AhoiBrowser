"use strict";

chrome.runtime.onInstalled.addListener(async ({ reason }) => {
  await chrome.storage.local.set({
    installed: true,
    installReason: reason,
    extensionId: chrome.runtime.id,
  });
});

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (message !== "ahoibrowser-phase0-status") {
    return false;
  }

  Promise.all([
    chrome.runtime.getPlatformInfo(),
    chrome.storage.local.get(["installed", "installReason", "extensionId"]),
    chrome.tabs.query({ active: true, currentWindow: true }),
  ])
    .then(([platform, stored, tabs]) => {
      sendResponse({
        ok: true,
        manifestVersion: chrome.runtime.getManifest().manifest_version,
        platform,
        stored,
        activeTabCount: tabs.length,
      });
    })
    .catch((error) => {
      sendResponse({ ok: false, error: String(error) });
    });
  return true;
});
