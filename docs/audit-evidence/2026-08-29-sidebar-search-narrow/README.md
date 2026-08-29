# Narrow sidebar search — visible acceptance

- Candidate: installed and signed AhoiBrowser built from `c6ef64604d2e38d829dab057bc00d751aef49a36`
- Installation receipt: `artifacts/install/ahoi-dev-c6ef646-20260829.json`
- Viewport state: sidebar resized through its native accessibility action to `208` DIP
- Test order: visible Computer Use journey first; focused programmatic regression follows only after this acceptance

## Journey

1. Opened the sidebar search at 208 DIP and confirmed that the icon, placeholder and close button remain inset inside the rounded shell without clipping. See `01-narrow-search-open.png`.
2. Focused the real text field, typed `MDN`, and confirmed that the existing sidebar hierarchy is filtered in place while matching supplemental results remain below it. See `02-narrow-search-filtered.jpeg`.
3. Activated `Suche löschen`; the full hierarchy returned and the control changed back to `Schließen`.
4. Activated `Schließen`; the search header and supplemental result section disappeared and the normal sidebar returned.

## Result

Visible acceptance passed. The narrow-header padding regression from the supplied screenshot is no longer reproducible on the exact installed candidate.
