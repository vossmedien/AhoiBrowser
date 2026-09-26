# 092 – Build 40 crash: dialog teardown empties the whole Widget, not the dialog

Status: integrated (desktop, 6230a31, build 41)
Owner lane: desktop (apply, build, test)
Base: HEAD (`git apply --check` passes at `56b5a36`).
Follows the H2 focus finding. Reviewed against owner commit `ebaf599`.

## Evidence

Installed build 40 (`6acd207`), `http-auth-journey.sh`, 10:4x on 26 Sep:
`Segmentation fault: 11`. From
`artifacts/computer-use/m153/http-auth-journey-installed-6acd207-20260926/browser.log`:

```
Received signal 11 SEGV_ACCERR 3d6368c8b87a640f
4 libui_views.dylib views::Widget::HandleWidgetDestroying() + 208
5 libui_views.dylib views::Widget::~Widget() + 500
6 libui_views.dylib views::(anonymous namespace)::BubbleWidget::~BubbleWidget()
7 libchrome_dll.dylib ...BrowserSidebarHostView::OnWorkspaceDialogClosed()::$_0
```

`HandleWidgetDestroying() + 208` resolves to `ui/views/widget/widget.cc:3038:7`,
which is `widget_delegate_->WindowClosing();`. The lookup used lldb on
`out/AhoiDev/libui_views.dylib`, whose file is unchanged since the build. The
Widget calls into a delegate or client structure that is already destroyed. The
fault address is a freed-slot pattern. The journey then reports
`own_sessions_signin: FAIL:workspace-not-created` and the follow-on steps fail.

## Cause

`ebaf599` added `PrepareDialogWidgetForDestruction(widget, remove_views=true)`.
That call runs `widget->GetContentsView()->RemoveAllChildViews()`. For a
dialog, `Widget::GetContentsView()` is the root view's contents, and that is
the **NonClientView** (`widget.cc:618`,
`root_view_->SetContentsView(non_client_view_)`). Emptying it destroys the
ClientView (DialogClientView) and the frame view while the Widget still holds
`non_client_view_` and its delegate. `~Widget` then runs `WindowClosing()` on
that torn-down structure.

The dialog's own body, where the text fields live, is
`Widget::GetClientContentsView()` (`widget.cc:864`): the client view's first
child.

## Change (`092-dialog-teardown-client-contents.patch`)

One line in `PrepareDialogWidgetForDestruction`: `GetContentsView()` becomes
`GetClientContentsView()`, plus a comment. The text fields are still destroyed
before `~Widget`, which was the purpose of `ebaf599`. The ClientView, frame and
delegate stay intact. The same helper is used for the group dialog and the
archive search (`browser_sidebar_host_group_dialog.cc:569`,
`browser_sidebar_host_archive.cc:192`), so they get the same fix.

Not compiled by this lane, which is on the build lock. `GetClientContentsView()`
is a public `const` accessor returning `View*`.

## Tests for the owner

- `http-auth-journey.sh` on the next build (the "Erstellen" path with own
  website sessions), several runs including one with the app not frontmost,
  since that was the build 39 condition.
- `keyboard-shortcuts-journey.sh` (build 40 aborted early with `RESULTS[*]:
  unbound variable` after `setupFailed: did not load .../gamma.html`); probably
  unrelated, but it runs the same dialogs.
