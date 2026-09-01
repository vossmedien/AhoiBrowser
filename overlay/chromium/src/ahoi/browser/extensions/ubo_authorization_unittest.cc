// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_authorization.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "ahoi/browser/extensions/ubo_migration_state.h"
#include "base/base64.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "crypto/hash.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/mojom/manifest.mojom-shared.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::extensions {

namespace {

constexpr char kPackageHash[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kPinnedCrxPublicKeyBase64[] =
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsgdkJHEX8xHAytYy3Rih"
    "qn5FoU/cbPKhoorkCCsgF8HR2y2OSWGM1Ojrmnr0ebgM9WA1pl1hr1CmOH7DjgQ"
    "VKRhzBjK7/Zb6RVJNPVGEQvV9CdCUwOTKsu1qQGRbjm9Z/DkYxgu6B2sLo0ZpQ/"
    "IBsmBvs+FGR4CqrWra8GZPwn7n3FibeoxcArWiAx85N2Oyiaef2Geytoog4hS+I"
    "5Fs3ymKkEeTYM3tzeC0U5nZ010LCnlQe0cQ3UDOro8VzLosuhaxAsrPFErIOfIUf"
    "vV3sNhQJrySqgii9Xv6RWT8TI3pHL1yjevKKTxNb2VbPlTOi5MyzPowWV8hHJEO"
    "kwq2dQIDAQAB";

class UboAuthorizationTest : public ::testing::Test {
 public:
  void SetUp() override { RegisterProfilePrefs(prefs_.registry()); }

 protected:
  scoped_refptr<const ::extensions::Extension> MakeExtension(
      int manifest_version = 2,
      std::string id = kUboClassicExtensionId,
      std::string version = "1.55.0",
      bool include_update_url = false,
      uint8_t key_byte = 0x42) {
    std::array<uint8_t, 32> key;
    key.fill(key_byte);
    ::extensions::ExtensionBuilder builder("uBlock Origin");
    builder.SetManifestVersion(manifest_version)
        .SetVersion(version)
        .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
        .SetID(id)
        .SetManifestKey("key", base::Base64Encode(key));
    if (include_update_url) {
      builder.SetManifestKey("update_url",
                             "https://attacker.example/update.xml");
    }
    return builder.Build();
  }

  scoped_refptr<const ::extensions::Extension> MakePinnedBootstrapExtension() {
    return ::extensions::ExtensionBuilder("uBlock Origin")
        .SetManifestVersion(2)
        .SetVersion(kUboClassicVersion)
        .SetLocation(::extensions::mojom::ManifestLocation::kInternal)
        .SetID(kUboClassicExtensionId)
        .SetManifestKey("key", kPinnedCrxPublicKeyBase64)
        .Build();
  }

  std::string KeyHash(uint8_t key_byte = 0x42) {
    std::array<uint8_t, 32> key;
    key.fill(key_byte);
    return base::HexEncodeLower(crypto::hash::Sha256(key));
  }

  UboCatalogEntry MakeEntry(uint64_t sequence = 1,
                            std::string version = "1.55.0",
                            std::string package_hash = kPackageHash) {
    UboCatalogEntry entry;
    entry.sequence = sequence;
    entry.extension_id = kUboClassicExtensionId;
    entry.version = base::Version(version);
    entry.package_sha256 = std::move(package_hash);
    entry.crx_public_key_sha256 = KeyHash();
    entry.update_manifest_url =
        GURL("https://updates.ahoi.example/ubo-update.xml");
    return entry;
  }

  VerifiedUboPackage MakePackage(const UboCatalogEntry& entry) {
    return VerifiedUboPackage{
        .extension_id = entry.extension_id,
        .package_sha256 = entry.package_sha256,
        .crx_public_key_sha256 = entry.crx_public_key_sha256,
    };
  }

  sync_preferences::TestingPrefServiceSyncable prefs_;
};

TEST_F(UboAuthorizationTest, AllowsOnlyMatchingPendingThenCommittedMv2) {
  UboCatalogEntry entry = MakeEntry();
  scoped_refptr<const ::extensions::Extension> ubo = MakeExtension();
  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));

  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_TRUE(authorization.has_value());
  EXPECT_TRUE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));
  ASSERT_TRUE((*authorization)->Commit(*ubo).has_value());
  EXPECT_TRUE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));

  std::optional<UboAuthorizationState> state =
      ReadCommittedUboAuthorization(prefs_);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(1u, state->sequence);
  EXPECT_EQ("1.55.0", state->version.GetString());
}

TEST_F(UboAuthorizationTest, AuthorizationAndMigrationPrefsAreLocalOnly) {
  EXPECT_EQ(0u, prefs_.registry()->GetRegistrationFlags(kUboAuthorizationPref));
  EXPECT_EQ(0u, prefs_.registry()->GetRegistrationFlags(kUboMigrationPref));
}

TEST_F(UboAuthorizationTest,
       MigrationCheckpointRoundTripsExactAuthorizationLocally) {
  UboCatalogEntry entry = GetPinnedUboBootstrapCatalogEntry();
  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE(
      (*authorization)->Commit(*MakePinnedBootstrapExtension()).has_value());
  std::optional<UboAuthorizationState> committed =
      ReadCommittedUboAuthorization(prefs_);
  ASSERT_TRUE(committed);

  ASSERT_TRUE(WriteUboPersistedMigrationState(&prefs_, *committed,
                                              "browser-process-a"));
  std::optional<UboPersistedMigrationState> migration =
      ReadUboPersistedMigrationState(prefs_);
  ASSERT_TRUE(migration);
  EXPECT_EQ("browser-process-a", migration->install_process_token);
  EXPECT_TRUE(UboMigrationMatchesAuthorization(*migration, *committed));
}

TEST_F(UboAuthorizationTest, AllowsExactPinnedBootstrapWithNoGuessedUpdateUrl) {
  UboCatalogEntry entry = GetPinnedUboBootstrapCatalogEntry();
  scoped_refptr<const ::extensions::Extension> extension =
      MakePinnedBootstrapExtension();

  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_TRUE(authorization.has_value());
  EXPECT_TRUE(IsUboManifestV2ExtensionAllowed(prefs_, *extension));
  ASSERT_TRUE((*authorization)->Commit(*extension).has_value());

  std::optional<UboAuthorizationState> state =
      ReadCommittedUboAuthorization(prefs_);
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(state->update_manifest_url.is_empty());
  EXPECT_EQ(kUboClassicCrxPublicKeySha256, state->crx_public_key_sha256);
}

TEST_F(UboAuthorizationTest,
       RejectsSchemaOneAndFormerIdentityAuthorizationState) {
  auto authorization_state = []() {
    return base::DictValue()
        .Set("schema_version", 1)
        .Set("sequence", base::NumberToString(kUboClassicBootstrapSequence))
        .Set("extension_id", kUboClassicExtensionId)
        .Set("version", kUboClassicVersion)
        .Set("package_sha256", kUboClassicPackageSha256)
        .Set("crx_public_key_sha256", kUboClassicCrxPublicKeySha256)
        .Set("update_manifest_url", "");
  };

  prefs_.SetDict(kUboAuthorizationPref, authorization_state());
  EXPECT_FALSE(ReadCommittedUboAuthorization(prefs_));
  EXPECT_FALSE(
      IsUboManifestV2ExtensionAllowed(prefs_, *MakePinnedBootstrapExtension()));

  base::DictValue former_identity = authorization_state();
  former_identity.Set("schema_version", 2);
  former_identity.Set("extension_id", kUboFormerClassicWebStoreExtensionId);
  prefs_.SetDict(kUboAuthorizationPref, std::move(former_identity));
  EXPECT_FALSE(ReadCommittedUboAuthorization(prefs_));
  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(
      prefs_, *MakeExtension(2, kUboFormerClassicWebStoreExtensionId,
                             kUboClassicVersion)));
}

TEST_F(UboAuthorizationTest, RejectsModifiedBootstrapMetadata) {
  UboCatalogEntry entry = GetPinnedUboBootstrapCatalogEntry();
  entry.upstream_commit[0] = entry.upstream_commit[0] == '0' ? '1' : '0';

  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_FALSE(authorization.has_value());
  EXPECT_EQ(UboVerificationError::kInstalledExtensionMismatch,
            authorization.error());
}

TEST_F(UboAuthorizationTest, AbortedTransactionLeavesNoMv2Exception) {
  UboCatalogEntry entry = MakeEntry();
  scoped_refptr<const ::extensions::Extension> ubo = MakeExtension();
  {
    auto authorization =
        BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
    ASSERT_TRUE(authorization.has_value());
    EXPECT_TRUE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));
  }
  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));
}

TEST_F(UboAuthorizationTest,
       ProfileShutdownClearsPendingAddressWithoutCommitting) {
  UboCatalogEntry entry = MakeEntry();
  scoped_refptr<const ::extensions::Extension> ubo = MakeExtension();
  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_TRUE(authorization.has_value());
  ASSERT_TRUE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));

  ClearPendingUboInstallAuthorization(&prefs_);

  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(prefs_, *ubo));
  EXPECT_FALSE((*authorization)->Commit(*ubo).has_value());
  EXPECT_FALSE(ReadCommittedUboAuthorization(prefs_));
}

TEST_F(UboAuthorizationTest, RejectsForeignMv2ManipulatedKeyAndUpdateUrl) {
  UboCatalogEntry entry = MakeEntry();
  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_TRUE(authorization.has_value());

  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(
      prefs_, *MakeExtension(2, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(
      prefs_,
      *MakeExtension(2, kUboClassicExtensionId, "1.55.0", false, 0x43)));
  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(
      prefs_, *MakeExtension(2, kUboClassicExtensionId, "1.55.0", true)));
}

TEST_F(UboAuthorizationTest, Mv3NeverReceivesTheMv2Exception) {
  UboCatalogEntry entry = MakeEntry();
  auto authorization =
      BeginUboInstallAuthorization(&prefs_, entry, MakePackage(entry));
  ASSERT_TRUE(authorization.has_value());

  EXPECT_FALSE(IsUboManifestV2ExtensionAllowed(prefs_, *MakeExtension(3)));
}

TEST_F(UboAuthorizationTest, RejectsSequenceAndVersionDowngrades) {
  UboCatalogEntry committed_entry = MakeEntry(10, "1.55.0");
  scoped_refptr<const ::extensions::Extension> committed_extension =
      MakeExtension(2, kUboClassicExtensionId, "1.55.0");
  auto committed = BeginUboInstallAuthorization(&prefs_, committed_entry,
                                                MakePackage(committed_entry));
  ASSERT_TRUE(committed.has_value());
  ASSERT_TRUE((*committed)->Commit(*committed_extension).has_value());

  UboCatalogEntry old_sequence = MakeEntry(9, "1.56.0");
  auto sequence_result = BeginUboInstallAuthorization(
      &prefs_, old_sequence, MakePackage(old_sequence));
  ASSERT_FALSE(sequence_result.has_value());
  EXPECT_EQ(UboVerificationError::kRollback, sequence_result.error());

  UboCatalogEntry old_version = MakeEntry(11, "1.54.0");
  auto version_result = BeginUboInstallAuthorization(&prefs_, old_version,
                                                     MakePackage(old_version));
  ASSERT_FALSE(version_result.has_value());
  EXPECT_EQ(UboVerificationError::kRollback, version_result.error());
}

TEST_F(UboAuthorizationTest, RejectsSameVersionRepackWithDifferentHash) {
  UboCatalogEntry committed_entry = MakeEntry(10);
  auto committed = BeginUboInstallAuthorization(&prefs_, committed_entry,
                                                MakePackage(committed_entry));
  ASSERT_TRUE(committed.has_value());
  ASSERT_TRUE((*committed)->Commit(*MakeExtension()).has_value());

  UboCatalogEntry repack = MakeEntry(
      11, "1.55.0",
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  auto result =
      BeginUboInstallAuthorization(&prefs_, repack, MakePackage(repack));
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(UboVerificationError::kRollback, result.error());
}

}  // namespace

}  // namespace ahoi::extensions
