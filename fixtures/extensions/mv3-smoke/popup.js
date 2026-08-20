"use strict";

const result = document.querySelector("#result");
chrome.runtime.sendMessage("ahoibrowser-phase0-status", (response) => {
  if (chrome.runtime.lastError) {
    result.textContent = `FAIL: ${chrome.runtime.lastError.message}`;
    return;
  }
  result.textContent = JSON.stringify(response, null, 2);
});
