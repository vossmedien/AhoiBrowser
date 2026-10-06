// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/ui/toast/ahoi_toast.h"

#include "base/test/icu_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::toast {
namespace {

TEST(AhoiToastTest, GermanMessagesNameCountAndTarget) {
  base::test::ScopedRestoreICUDefaultLocale locale("de");
  EXPECT_EQ(u"Im Hintergrund geöffnet", Message(Event::kBackgroundTabOpened));
  EXPECT_EQ(u"Tab archiviert", Message(Event::kArchived));
  EXPECT_EQ(u"3 Tabs archiviert", Message(Event::kArchived, u"", 3));
  EXPECT_EQ(u"2 Tabs nach „Arbeit“ verschoben",
            Message(Event::kMovedToWorkspace, u"Arbeit", 2));
  EXPECT_EQ(u"Tab verschoben", Message(Event::kMovedToWorkspace));
  EXPECT_EQ(u"Lädt herunter: a.pdf", Message(Event::kDownloadStarted, u"a.pdf"));
}

TEST(AhoiToastTest, EnglishMessages) {
  base::test::ScopedRestoreICUDefaultLocale locale("en_US");
  EXPECT_EQ(u"Link copied", Message(Event::kLinkCopied));
  EXPECT_EQ(u"4 tabs closed", Message(Event::kClosed, u"", 4));
  EXPECT_EQ(u"Tab moved to “Work”", Message(Event::kMovedToWorkspace, u"Work"));
}

}  // namespace
}  // namespace ahoi::toast
