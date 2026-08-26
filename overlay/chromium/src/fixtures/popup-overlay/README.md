# Popup overlay fixture

Serve this directory over HTTP so `window.open` uses an ordinary web origin:

```sh
python3 -m http.server 8765 --directory fixtures/popup-overlay
```

Open `http://127.0.0.1:8765/` in a normal AhoiBrowser pane. The ordinary popup
exercises POPUP-01 through POPUP-05 state retention. The sign-in and large
window actions exercise the native-window fallback used by POPUP-06. Close the
opener and child in both orders for POPUP-07 cleanup.
