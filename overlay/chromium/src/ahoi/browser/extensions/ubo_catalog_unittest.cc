// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/extensions/ubo_catalog.h"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "base/values.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi::extensions {

namespace {

constexpr int64_t kNowSeconds = 2000000000;
constexpr char kPackageHash[] =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr char kCrxKeyHash[] =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

struct SigningFixture {
  SigningFixture()
      : private_key(crypto::keypair::PrivateKey::GenerateEd25519()),
        public_key(crypto::keypair::PublicKey::FromPrivateKey(private_key)) {}

  crypto::keypair::PrivateKey private_key;
  crypto::keypair::PublicKey public_key;
};

UboProductConfig MakeConfig(const SigningFixture& keys) {
  UboProductConfig config;
  config.catalog_url = GURL("https://updates.ahoi.example/catalog.json");
  config.artifact_origin = GURL("https://updates.ahoi.example/");
  config.catalog_public_key = keys.public_key.ToEd25519PublicKey();
  return config;
}

base::DictValue MakePayload() {
  return base::DictValue()
      .Set("schema_version", 1)
      .Set("sequence", "42")
      .Set("valid_from", "1999999900")
      .Set("valid_until", "2000001000")
      .Set("extension_id", kUboClassicExtensionId)
      .Set("version", "1.55.0")
      .Set("package_url", "https://updates.ahoi.example/ubo-1.55.0.crx")
      .Set("update_manifest_url", "https://updates.ahoi.example/ubo-update.xml")
      .Set("sha256", kPackageHash)
      .Set("crx_public_key_sha256", kCrxKeyHash)
      .Set("upstream_tag", "1.55.0")
      .Set("upstream_source_url",
           "https://github.com/gorhill/uBlock/releases/tag/1.55.0")
      .Set("license", kUboLicense);
}

std::string SignPayload(const SigningFixture& keys,
                        base::DictValue payload_dict) {
  std::string payload = base::WriteJson(payload_dict).value();
  std::vector<uint8_t> signature = crypto::sign::Sign(
      crypto::sign::ED25519, keys.private_key, base::as_byte_span(payload));
  return base::WriteJson(base::DictValue()
                             .Set("payload", payload)
                             .Set("signature", base::Base64Encode(signature)))
      .value();
}

base::Time TestNow() {
  return base::Time::FromSecondsSinceUnixEpoch(kNowSeconds);
}

TEST(UboCatalogTest, AcceptsExactlySignedAllowlistedCatalog) {
  SigningFixture keys;
  UboProductConfig config = MakeConfig(keys);

  auto result = VerifyUboCatalog(config, config.catalog_url,
                                 SignPayload(keys, MakePayload()), TestNow());

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(42u, result->sequence);
  EXPECT_EQ(kUboClassicExtensionId, result->extension_id);
  EXPECT_EQ("1.55.0", result->version.GetString());
  EXPECT_EQ(kPackageHash, result->package_sha256);
}

TEST(UboCatalogTest, RejectsPayloadTamperingAfterSignature) {
  SigningFixture keys;
  UboProductConfig config = MakeConfig(keys);
  std::string envelope = SignPayload(keys, MakePayload());
  size_t version = envelope.find("1.55.0");
  ASSERT_NE(std::string::npos, version);
  envelope.replace(version, 6, "1.54.0");

  auto result =
      VerifyUboCatalog(config, config.catalog_url, envelope, TestNow());

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(UboVerificationError::kInvalidSignature, result.error());
}

TEST(UboCatalogTest, RejectsForeignMv2IdentityEvenWhenSigned) {
  SigningFixture keys;
  UboProductConfig config = MakeConfig(keys);
  base::DictValue payload = MakePayload();
  payload.Set("extension_id", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

  auto result =
      VerifyUboCatalog(config, config.catalog_url,
                       SignPayload(keys, std::move(payload)), TestNow());

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(UboVerificationError::kUnexpectedExtensionId, result.error());
}

TEST(UboCatalogTest, RejectsInsecureOrRedirectedArtifactsAndManifests) {
  SigningFixture keys;
  UboProductConfig config = MakeConfig(keys);
  base::DictValue insecure_package = MakePayload();
  insecure_package.Set("package_url", "http://updates.ahoi.example/ubo.crx");
  auto package_result = VerifyUboCatalog(
      config, config.catalog_url,
      SignPayload(keys, std::move(insecure_package)), TestNow());
  ASSERT_FALSE(package_result.has_value());
  EXPECT_EQ(UboVerificationError::kInsecureOrUnexpectedArtifactUrl,
            package_result.error());

  base::DictValue credentialed_package = MakePayload();
  credentialed_package.Set("package_url",
                           "https://user:secret@updates.ahoi.example/ubo.crx");
  auto credential_result = VerifyUboCatalog(
      config, config.catalog_url,
      SignPayload(keys, std::move(credentialed_package)), TestNow());
  ASSERT_FALSE(credential_result.has_value());
  EXPECT_EQ(UboVerificationError::kInsecureOrUnexpectedArtifactUrl,
            credential_result.error());

  base::DictValue foreign_manifest = MakePayload();
  foreign_manifest.Set("update_manifest_url",
                       "https://attacker.example/update.xml");
  auto manifest_result = VerifyUboCatalog(
      config, config.catalog_url,
      SignPayload(keys, std::move(foreign_manifest)), TestNow());
  ASSERT_FALSE(manifest_result.has_value());
  EXPECT_EQ(UboVerificationError::kInvalidUpdateManifestUrl,
            manifest_result.error());
}

TEST(UboCatalogTest, ProductionConfigFailsClosedUntilProvisioned) {
  UboProductConfig config = GetProductionUboProductConfig();
  EXPECT_FALSE(config.IsProvisioned());
  auto result = VerifyUboCatalog(
      config, GURL("https://updates.ahoi.example/catalog.json"), "{}",
      TestNow());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(UboVerificationError::kNotProvisioned, result.error());
}

}  // namespace

}  // namespace ahoi::extensions
