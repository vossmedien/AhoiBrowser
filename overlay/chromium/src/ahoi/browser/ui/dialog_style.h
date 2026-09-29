// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_UI_DIALOG_STYLE_H_
#define AHOI_BROWSER_UI_DIALOG_STYLE_H_

#include <memory>
#include <string>

#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/insets.h"

namespace views {
class DialogDelegate;
class Label;
class LabelButton;
class Textfield;
class View;
}  // namespace views

namespace ahoi::dialog_style {

// One modal dialog language for Ahoi's own Views dialogs (design spec
// 2026-09-29): panel radius, 24 padding, 36-high fields, 32-high buttons,
// a 20/25 semibold title and a 2 pt accent focus ring with a 2 pt gap.
// Chromium keeps owning the dialog, its buttons and its focus handling;
// these helpers only set the documented DialogDelegate/View properties.

// Content margins below the title; the button row follows directly.
gfx::Insets DialogContentMargins();
gfx::Insets DialogTitleMargins();
gfx::Insets DialogButtonRowInsets();
gfx::FontList DialogTitleFontList();

// Before the widget exists: width, margins and corner radius.
void ApplyDialogFrame(views::DialogDelegate& delegate, int width);

// After the widget exists: title typography, button row, button heights and
// focus rings. Safe to call when a part (title, a button) is absent.
void ApplyDialogChrome(views::DialogDelegate& delegate);

void StyleDialogTitle(views::Label& title);
void StyleDialogButton(views::LabelButton& button);
// Fixes the field height and gives it the spec focus ring.
void StyleDialogField(views::Textfield& field);
// 2 pt accent ring with a 2 pt gap on `view`, if it has a FocusRing.
void StyleFocusRing(views::View& view);

// A multi-line error message in the semantic error colour, hidden until
// ShowFieldError() is called.
std::unique_ptr<views::Label> CreateFieldErrorLabel();
// Marks `field` invalid (1 pt error outline), shows `message` in
// `error_label`, announces it and keeps focus in the field. An empty
// `message` clears the error again.
void ShowFieldError(views::Textfield& field,
                    views::Label& error_label,
                    const std::u16string& message);

}  // namespace ahoi::dialog_style

#endif  // AHOI_BROWSER_UI_DIALOG_STYLE_H_
