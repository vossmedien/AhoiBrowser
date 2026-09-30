// MOB-EXT-01: proves content-script injection and extension storage.
document.documentElement.dataset.ahoiSpike = "1";
const visitsStored = browser.storage.local.get("visits").then((stored) => {
  const visits = (stored.visits || 0) + 1;
  document.documentElement.dataset.ahoiSpikeVisits = String(visits);
  return browser.storage.local.set({ visits }).then(() => visits);
});

// Visible self-check, only on pages that ask for it with
// `?ahoi-spike-check`: two script probes that differ only in the path the
// rule in rules.json blocks. The control must load, the other must not.
if (location.search.includes("ahoi-spike-check")) {
  const probe = (name) => new Promise((resolve) => {
    const script = document.createElement("script");
    script.src = location.origin + "/?ahoi-spike-probe=" + name;
    script.onload = () => resolve("loaded");
    script.onerror = () => resolve("failed");
    document.documentElement.appendChild(script);
  });
  const ready = new Promise((resolve) => {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", resolve, { once: true });
    } else {
      resolve();
    }
  });
  Promise.all([
    visitsStored,
    probe("control.js"),
    probe("/ahoi-spike-blocked.js"),
    ready,
  ]).then(([visits, control, blocked]) => {
    const report = document.createElement("p");
    report.id = "ahoi-spike-report";
    report.setAttribute("role", "status");
    report.textContent = "Ahoi Spike report: content-script=ran"
      + " visits=" + visits
      + " control=" + control
      + " rule=" + (blocked === "failed" ? "blocked" : "not-blocked");
    report.style.cssText = "font:600 18px -apple-system,sans-serif;"
      + "padding:12px;margin:12px;border-radius:10px;"
      + "background:#f97316;color:white";
    document.body.prepend(report);
  });
}
