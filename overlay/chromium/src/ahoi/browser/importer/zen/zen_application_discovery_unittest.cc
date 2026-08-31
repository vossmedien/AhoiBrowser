// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/zen/zen_application_discovery.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::zen::internal {

namespace {

constexpr std::string_view kOfficialZenBundleIdentifier =
    "app.zen-browser.zen";

base::FilePath CreateZenBundle(const base::FilePath& root,
                               std::string_view name,
                               std::string_view bundle_identifier =
                                   kOfficialZenBundleIdentifier) {
  const base::FilePath bundle = root.AppendASCII(name);
  const base::FilePath executable = bundle.AppendASCII("Contents/MacOS/zen");
  EXPECT_TRUE(base::CreateDirectory(executable.DirName()));
  EXPECT_TRUE(base::WriteFile(executable, "fixture"));
  if (!bundle_identifier.empty()) {
    std::string info_plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\"><dict><key>CFBundleIdentifier</key><string>";
    info_plist.append(bundle_identifier);
    info_plist.append("</string></dict></plist>\n");
    EXPECT_TRUE(base::WriteFile(
        bundle.AppendASCII("Contents/Info.plist"), info_plist));
  }
  return bundle;
}

TEST(ZenApplicationDiscoveryTest, FindsExactSafeBundleAndRunningExecutable) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath applications =
      temp_dir.GetPath().AppendASCII("Applications");
  ASSERT_TRUE(base::CreateDirectory(applications));
  const base::FilePath bundle = CreateZenBundle(applications, "Zen.app");

  const ZenApplicationState state = InspectZenApplicationAt(
      {applications}, {bundle.AppendASCII("Contents/MacOS/zen")});
  EXPECT_TRUE(state.installed);
  EXPECT_TRUE(state.running);
  EXPECT_EQ(state.bundle_path, bundle);
  EXPECT_EQ(GetZenImportAvailability(state),
            ZenImportAvailability::kSourceRunning);
}

TEST(ZenApplicationDiscoveryTest, DistinguishesImportAvailabilityReasons) {
  EXPECT_EQ(GetZenImportAvailability({}),
            ZenImportAvailability::kNotInstalled);

  ZenApplicationState installed;
  installed.bundle_path =
      base::FilePath(FILE_PATH_LITERAL("/Applications/Zen.app"));
  installed.installed = true;
  EXPECT_EQ(GetZenImportAvailability(installed),
            ZenImportAvailability::kAvailable);

  installed.running = true;
  EXPECT_EQ(GetZenImportAvailability(installed),
            ZenImportAvailability::kSourceRunning);
}

TEST(ZenApplicationDiscoveryTest, RejectsSimilarNamesAndStaleLockEvidence) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath applications =
      temp_dir.GetPath().AppendASCII("Applications");
  ASSERT_TRUE(base::CreateDirectory(applications));
  const base::FilePath backup = CreateZenBundle(applications, "Zen.app.backup");
  ASSERT_TRUE(
      base::WriteFile(temp_dir.GetPath().AppendASCII("parent.lock"), "stale"));

  const ZenApplicationState state = InspectZenApplicationAt(
      {applications}, {backup.AppendASCII("Contents/MacOS/zen")});
  EXPECT_FALSE(state.installed);
  EXPECT_FALSE(state.running);
  EXPECT_TRUE(state.bundle_path.empty());
}

TEST(ZenApplicationDiscoveryTest, RejectsSymlinkedBundle) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath applications =
      temp_dir.GetPath().AppendASCII("Applications");
  const base::FilePath external =
      temp_dir.GetPath().AppendASCII("ExternalApplications");
  ASSERT_TRUE(base::CreateDirectory(applications));
  ASSERT_TRUE(base::CreateDirectory(external));
  const base::FilePath external_bundle = CreateZenBundle(external, "Zen.app");
  ASSERT_TRUE(base::CreateSymbolicLink(external_bundle,
                                       applications.AppendASCII("Zen.app")));

  const ZenApplicationState state = InspectZenApplicationAt({applications}, {});
  EXPECT_FALSE(state.installed);
  EXPECT_TRUE(state.bundle_path.empty());
}

TEST(ZenApplicationDiscoveryTest,
     RejectsMissingUnexpectedOrSymlinkedBundleIdentifier) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath applications =
      temp_dir.GetPath().AppendASCII("Applications");
  ASSERT_TRUE(base::CreateDirectory(applications));
  CreateZenBundle(applications, "Zen.app", /*bundle_identifier=*/"");
  CreateZenBundle(applications, "Zen Browser.app", "com.example.zen");
  const base::FilePath twilight =
      CreateZenBundle(applications, "Zen Twilight.app");
  const base::FilePath external_info =
      temp_dir.GetPath().AppendASCII("ExternalInfo.plist");
  ASSERT_TRUE(base::WriteFile(
      external_info,
      "<plist><dict><key>CFBundleIdentifier</key>"
      "<string>app.zen-browser.zen</string></dict></plist>"));
  ASSERT_TRUE(base::DeleteFile(
      twilight.AppendASCII("Contents/Info.plist")));
  ASSERT_TRUE(base::CreateSymbolicLink(
      external_info, twilight.AppendASCII("Contents/Info.plist")));

  const ZenApplicationState state = InspectZenApplicationAt({applications}, {});
  EXPECT_FALSE(state.installed);
  EXPECT_TRUE(state.bundle_path.empty());
}

TEST(ZenApplicationDiscoveryTest,
     RecognizesOnlyExecutablesInsideAuthenticatedExactBundle) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath applications =
      temp_dir.GetPath().AppendASCII("Applications");
  const base::FilePath untrusted =
      temp_dir.GetPath().AppendASCII("UntrustedApplications");
  ASSERT_TRUE(base::CreateDirectory(applications));
  ASSERT_TRUE(base::CreateDirectory(untrusted));
  const base::FilePath bundle = CreateZenBundle(applications, "Zen.app");
  const base::FilePath twilight =
      CreateZenBundle(applications, "Zen Twilight.app");
  const base::FilePath spoofed =
      CreateZenBundle(untrusted, "Zen Browser.app", "com.example.zen");

  EXPECT_TRUE(IsZenBundleExecutablePath(
      bundle.AppendASCII("Contents/MacOS/zen")));
  EXPECT_TRUE(IsZenBundleExecutablePath(bundle.AppendASCII(
      "Contents/Frameworks/Zen Helper.app/Contents/MacOS/Zen Helper")));
  EXPECT_TRUE(IsZenBundleExecutablePath(
      twilight.AppendASCII("Contents/MacOS/zen")));
  EXPECT_FALSE(IsZenBundleExecutablePath(
      spoofed.AppendASCII("Contents/MacOS/zen")));
  EXPECT_FALSE(IsZenBundleExecutablePath(applications.AppendASCII(
      "Zen Helper.app/Contents/MacOS/Zen Helper")));
  EXPECT_FALSE(IsZenBundleExecutablePath(applications.AppendASCII(
      "Zen.app.backup/Contents/MacOS/zen")));
  EXPECT_FALSE(IsZenBundleExecutablePath(base::FilePath(
      FILE_PATH_LITERAL("relative/Zen.app/Contents/MacOS/zen"))));
}

}  // namespace

}  // namespace ahoi::importer::zen::internal
