// Reuse this file in the nonpersistent MV3 background page; retain five files.
if (typeof document !== "undefined" && document.getElementById("refresh")) {
  let refreshes = 0;
  const refresh = async () => {
    try {
      const stored = await browser.storage.local.get("visits");
      document.getElementById("visits").textContent = String(stored.visits || 0);
      document.getElementById("status").textContent = "Refresh " + refreshes;
    } catch {
      document.getElementById("status").textContent = "Storage permission required";
    }
  };
  document.getElementById("refresh").addEventListener("click", () => {
    refreshes += 1;
    refresh();
  });
  refresh();
} else {
  let port;
  let connecting = false;
  let reconnectRequested = false;
  const connect = async () => {
    if (port) return;
    if (connecting) { reconnectRequested = true; return; }
    connecting = true;
    try {
      if (!await browser.permissions.contains({permissions: ["nativeMessaging"]})) return;
      const connection = browser.runtime.connectNative("ahoi.spike.rules");
      port = connection;
      let updating = false;
      connection.onDisconnect.addListener(() => {
        if (port === connection) port = undefined;
      });
      connection.onMessage.addListener(async (message) => {
        if (port !== connection || updating || typeof message?.id !== "string"
            || typeof message.enabled !== "boolean") return;
        updating = true;
        let ok = false;
        try {
          await browser.declarativeNetRequest.updateEnabledRulesets({
            enableRulesetIds: message.enabled ? ["spike"] : [],
            disableRulesetIds: message.enabled ? [] : ["spike"],
          });
          const enabled = await browser.declarativeNetRequest.getEnabledRulesets();
          ok = enabled.includes("spike") === message.enabled;
        } catch (error) {
          console.error("Ahoi Spike rule update failed", error);
        } finally {
          updating = false;
          if (port === connection) {
            connection.postMessage({id: message.id, enabled: message.enabled, ok});
          }
        }
      });
      connection.postMessage({ready: true});
    } catch (error) {
      console.error("Ahoi Spike control connection failed", error);
    } finally {
      connecting = false;
      if (reconnectRequested) {
        reconnectRequested = false;
        connect(); // Consume a grant event that arrived during the first check.
      }
    }
  };
  browser.permissions.onAdded.addListener((added) => {
    if (added.permissions.includes("nativeMessaging")) connect();
  });
  browser.permissions.onRemoved.addListener((removed) => {
    if (!removed.permissions.includes("nativeMessaging")) return;
    // A closed native endpoint can remain in this background page's JS heap.
    // Clear it on the SDK event so the next explicit grant opens a fresh port.
    const previous = port;
    port = undefined;
    previous?.disconnect();
  });
  connect();
}
