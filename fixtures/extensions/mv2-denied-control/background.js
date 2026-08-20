"use strict";

chrome.runtime.onInstalled.addListener(() => {
  console.error("SECURITY FAILURE: non-allowlisted MV2 control was installed");
});
