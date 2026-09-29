browser.storage.local.get("visits").then((stored) => {
  document.getElementById("visits").textContent = String(stored.visits || 0);
});
