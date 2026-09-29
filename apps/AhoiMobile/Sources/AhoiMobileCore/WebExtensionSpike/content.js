// MOB-EXT-01: proves content-script injection and extension storage.
document.documentElement.dataset.ahoiSpike = "1";
browser.storage.local.get("visits").then((stored) => {
  const visits = (stored.visits || 0) + 1;
  document.documentElement.dataset.ahoiSpikeVisits = String(visits);
  return browser.storage.local.set({ visits });
});
