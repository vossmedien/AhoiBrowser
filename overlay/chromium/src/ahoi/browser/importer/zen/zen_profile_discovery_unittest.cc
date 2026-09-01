// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/importer/zen/zen_profile_discovery.h"

#include <sys/stat.h>

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::importer::zen {

namespace {

class ZenProfileDiscoveryTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::FilePath CreateRoot() {
    const base::FilePath root = temp_dir_.GetPath().AppendASCII("zen");
    EXPECT_TRUE(base::CreateDirectory(root));
    return root;
  }

  base::ScopedTempDir temp_dir_;
};

TEST_F(ZenProfileDiscoveryTest, FindsOnlyAvailableStandardCategories) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nName=Personal\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("places.sqlite"), "db"));

  const std::vector<ZenProfileDetail> profiles =
      DiscoverZenProfilesAtRoot(root);
  ASSERT_EQ(profiles.size(), 1u);
  EXPECT_TRUE(profiles[0].name.empty());
  EXPECT_EQ(profiles[0].services_supported,
            user_data_importer::HISTORY | user_data_importer::FAVORITES);
  EXPECT_EQ(profiles[0].structure_capability,
            ZenStructureCapability::kNotPresent);
}

TEST_F(ZenProfileDiscoveryTest, FindsProfilesAcrossNumberingGaps) {
  const base::FilePath root = CreateRoot();
  const base::FilePath personal = root.AppendASCII("Profiles/personal");
  const base::FilePath work = root.AppendASCII("Profiles/work");
  ASSERT_TRUE(base::CreateDirectory(personal));
  ASSERT_TRUE(base::CreateDirectory(work));
  ASSERT_TRUE(base::WriteFile(
      root.AppendASCII("profiles.ini"),
      "[Profile0]\nName=Personal\nIsRelative=1\nPath=Profiles/personal\n"
      "[Profile2]\nName=Work\nIsRelative=1\nPath=Profiles/work\n"));
  ASSERT_TRUE(base::WriteFile(personal.AppendASCII("places.sqlite"), "db"));
  ASSERT_TRUE(base::WriteFile(work.AppendASCII("places.sqlite"), "db"));

  const std::vector<ZenProfileDetail> profiles =
      DiscoverZenProfilesAtRoot(root);
  ASSERT_EQ(profiles.size(), 2u);
  EXPECT_EQ(profiles[0].name, u"Personal");
  EXPECT_EQ(profiles[0].path, personal);
  EXPECT_EQ(profiles[1].name, u"Work");
  EXPECT_EQ(profiles[1].path, work);
}

TEST_F(ZenProfileDiscoveryTest, RecognizesButDoesNotEnableMozLz4Structure) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  const std::string session = std::string("mozLz40\0", 8) + "fixture";
  ASSERT_TRUE(
      base::WriteFile(profile.AppendASCII(kZenSessionStoreName), session));

  const std::vector<ZenProfileDetail> profiles =
      DiscoverZenProfilesAtRoot(root);
  ASSERT_EQ(profiles.size(), 1u);
  EXPECT_EQ(profiles[0].services_supported, user_data_importer::NONE);
  EXPECT_EQ(profiles[0].structure_capability,
            ZenStructureCapability::kMozLz4Candidate);
}

TEST_F(ZenProfileDiscoveryTest, RejectsTraversalAndExternalAbsoluteProfiles) {
  const base::FilePath root = CreateRoot();
  const base::FilePath external = temp_dir_.GetPath().AppendASCII("external");
  ASSERT_TRUE(base::CreateDirectory(external));
  ASSERT_TRUE(base::WriteFile(external.AppendASCII("places.sqlite"), "db"));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\nPath=../external\n"
                              "[Profile1]\nIsRelative=0\nPath=" +
                                  external.AsUTF8Unsafe() + "\n"));

  EXPECT_TRUE(DiscoverZenProfilesAtRoot(root).empty());
}

TEST_F(ZenProfileDiscoveryTest, RejectsSymlinkedProfile) {
  const base::FilePath root = CreateRoot();
  const base::FilePath external = temp_dir_.GetPath().AppendASCII("external");
  ASSERT_TRUE(base::CreateDirectory(external));
  ASSERT_TRUE(base::WriteFile(external.AppendASCII("places.sqlite"), "db"));
  const base::FilePath link = root.AppendASCII("Profiles/linked");
  ASSERT_TRUE(base::CreateDirectory(link.DirName()));
  ASSERT_TRUE(base::CreateSymbolicLink(external, link));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\n"
                              "Path=Profiles/linked\n"));

  EXPECT_TRUE(DiscoverZenProfilesAtRoot(root).empty());
}

TEST_F(ZenProfileDiscoveryTest, RejectsNonRegularProfileData) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  const base::FilePath places = profile.AppendASCII("places.sqlite");
  ASSERT_EQ(mkfifo(places.value().c_str(), 0600), 0);

  EXPECT_TRUE(DiscoverZenProfilesAtRoot(root).empty());
}

TEST_F(ZenProfileDiscoveryTest, RejectsNonRegularProfilesIni) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profiles_ini = root.AppendASCII("profiles.ini");
  ASSERT_EQ(mkfifo(profiles_ini.value().c_str(), 0600), 0);

  EXPECT_TRUE(DiscoverZenProfilesAtRoot(root).empty());
}

TEST_F(ZenProfileDiscoveryTest, RejectsSymlinkedSessionStoreLeaf) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  const base::FilePath external = temp_dir_.GetPath().AppendASCII("session");
  ASSERT_TRUE(base::WriteFile(external, std::string("mozLz40\0", 8)));
  ASSERT_TRUE(base::CreateSymbolicLink(
      external, profile.AppendASCII(kZenSessionStoreName)));

  const std::vector<ZenProfileDetail> profiles =
      DiscoverZenProfilesAtRoot(root);
  ASSERT_EQ(profiles.size(), 1u);
  EXPECT_EQ(profiles[0].structure_capability,
            ZenStructureCapability::kUnsafeOrOversized);
}

TEST_F(ZenProfileDiscoveryTest, BuildsFirefoxCompatibleSourceProfile) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nName=Personal\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("places.sqlite"), "db"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("compatibility.ini"),
                              "[Compatibility]\nLastVersion=154.0.1_1\n"));

  std::vector<user_data_importer::SourceProfile> profiles;
  AppendZenSourceProfilesAtRoot(root, "de", &profiles);

  ASSERT_EQ(profiles.size(), 1u);
  EXPECT_EQ(profiles[0].importer_name, u"Zen");
  EXPECT_EQ(profiles[0].importer_type, user_data_importer::TYPE_FIREFOX);
  EXPECT_EQ(profiles[0].services_supported,
            user_data_importer::HISTORY | user_data_importer::FAVORITES);
  EXPECT_EQ(profiles[0].locale, "de");
}

TEST_F(ZenProfileDiscoveryTest, RejectsLegacyOrMissingCompatibilityVersion) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("places.sqlite"), "db"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("compatibility.ini"),
                              "[Compatibility]\nLastVersion=47.0\n"));

  std::vector<user_data_importer::SourceProfile> profiles;
  AppendZenSourceProfilesAtRoot(root, "de", &profiles);
  EXPECT_TRUE(profiles.empty());

  ASSERT_TRUE(base::DeleteFile(profile.AppendASCII("compatibility.ini")));
  AppendZenSourceProfilesAtRoot(root, "de", &profiles);
  EXPECT_TRUE(profiles.empty());
}

TEST_F(ZenProfileDiscoveryTest, DoesNotAppendAnUninstalledApplication) {
  ZenApplicationState application;
  std::vector<user_data_importer::SourceProfile> profiles;

  EXPECT_EQ(internal::AppendZenSourceProfilesForApplication(
                application, CreateRoot(), "de", &profiles),
            ZenImportAvailability::kNotInstalled);
  EXPECT_TRUE(profiles.empty());
}

TEST_F(ZenProfileDiscoveryTest, ReportsRunningApplicationWithoutAppending) {
  ZenApplicationState application;
  application.bundle_path = base::FilePath("/Applications/Zen.app");
  application.installed = true;
  application.running = true;
  std::vector<user_data_importer::SourceProfile> profiles;

  EXPECT_EQ(internal::AppendZenSourceProfilesForApplication(
                application, CreateRoot(), "de", &profiles),
            ZenImportAvailability::kSourceRunning);
  EXPECT_TRUE(profiles.empty());
}

TEST_F(ZenProfileDiscoveryTest, AppendsAvailableApplicationWithResourcePath) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nName=Personal\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("places.sqlite"), "db"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("compatibility.ini"),
                              "[Compatibility]\nLastVersion=154.0.1_1\n"));
  ZenApplicationState application;
  application.bundle_path = base::FilePath("/Applications/Zen.app");
  application.installed = true;
  std::vector<user_data_importer::SourceProfile> profiles;

  EXPECT_EQ(internal::AppendZenSourceProfilesForApplication(application, root,
                                                            "de", &profiles),
            ZenImportAvailability::kAvailable);
  ASSERT_EQ(profiles.size(), 1u);
  EXPECT_EQ(profiles[0].app_path,
            application.bundle_path.AppendASCII("Contents/Resources"));
}

TEST_F(ZenProfileDiscoveryTest,
       PreservesDetectionOnlyStructureMetadataForAvailableProfile) {
  const base::FilePath root = CreateRoot();
  const base::FilePath profile = root.AppendASCII("Profiles/default");
  ASSERT_TRUE(base::CreateDirectory(profile));
  ASSERT_TRUE(base::WriteFile(root.AppendASCII("profiles.ini"),
                              "[Profile0]\nIsRelative=1\n"
                              "Path=Profiles/default\n"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("places.sqlite"), "db"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII("compatibility.ini"),
                              "[Compatibility]\nLastVersion=154.0.1_1\n"));
  ASSERT_TRUE(base::WriteFile(profile.AppendASCII(kZenSessionStoreName),
                              std::string("mozLz40\0", 8) + "fixture"));
  ZenApplicationState application;
  application.bundle_path = base::FilePath("/Applications/Zen.app");
  application.installed = true;
  std::vector<user_data_importer::SourceProfile> profiles;

  const ZenSourceProfilesResult result =
      internal::AppendZenSourceProfilesForApplicationWithMetadata(
          application, root, "de", &profiles);

  EXPECT_EQ(result.availability, ZenImportAvailability::kAvailable);
  ASSERT_EQ(profiles.size(), 1u);
  ASSERT_EQ(result.metadata.size(), 1u);
  EXPECT_EQ(result.metadata[0].structure_capability,
            ZenStructureCapability::kMozLz4Candidate);
}

TEST_F(ZenProfileDiscoveryTest,
       InstalledApplicationWithoutSafeProfilesReturnsGlobalReason) {
  ZenApplicationState application;
  application.bundle_path = base::FilePath("/Applications/Zen.app");
  application.installed = true;
  std::vector<user_data_importer::SourceProfile> profiles;

  const ZenSourceProfilesResult result =
      internal::AppendZenSourceProfilesForApplicationWithMetadata(
          application, CreateRoot(), "de", &profiles);

  EXPECT_EQ(result.availability, ZenImportAvailability::kNoSafeProfiles);
  EXPECT_TRUE(result.metadata.empty());
  EXPECT_TRUE(profiles.empty());
}

}  // namespace

}  // namespace ahoi::importer::zen
