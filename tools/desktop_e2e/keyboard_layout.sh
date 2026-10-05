# Sourced by journeys that send layout-dependent key codes.
# current_keyboard_layout prints the active layout's input source ID, e.g.
# com.apple.keylayout.German. HIToolbox only stores
# AppleCurrentKeyboardLayoutInputSourceID after the user has switched input
# sources once; a Mac that has only ever had one layout (the Inhouse target,
# 5 October 2026: German, key absent) lists it only under the selected or
# enabled input sources. Falling back to QWERTY there sent Cmd+Y for undo.
current_keyboard_layout() {
  local prefs=~/Library/Preferences/com.apple.HIToolbox id key name
  id=$(defaults read "$prefs" AppleCurrentKeyboardLayoutInputSourceID \
    2>/dev/null)
  if [ -n "$id" ]; then echo "$id"; return; fi
  for key in AppleSelectedInputSources AppleEnabledInputSources; do
    name=$(defaults read "$prefs" "$key" 2>/dev/null \
      | sed -n 's/.*"KeyboardLayout Name" = "*\([^";]*\)"*;.*/\1/p' \
      | head -1)
    if [ -n "$name" ]; then
      # "U.S." is com.apple.keylayout.US, "Swiss German" ...SwissGerman.
      echo "com.apple.keylayout.$(echo "$name" | tr -d '. ')"
      return
    fi
  done
}
