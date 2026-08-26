// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_style_preprocessor.h"

#include <string>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace ahoi {
namespace {

TEST(DeveloperStylePreprocessorTest,
     CompilesLessVariablesNestingAndParentSelectors) {
  DeveloperStyleCompileResult result = CompileDeveloperStyleSource(
      {.language = DeveloperStyleLanguage::kLess, .source = R"(@brand: #0a84ff;
.card, .panel {
  color: @brand;
  .title { font-weight: 700; }
  &:hover { border-color: @brand; }
})"});
  ASSERT_TRUE(result.succeeded());
  EXPECT_EQ(result.css,
            ".card,.panel{color: #0a84ff;}\n"
            ".card .title,.panel .title{font-weight: 700;}\n"
            ".card:hover,.panel:hover{border-color: #0a84ff;}\n");
}

TEST(DeveloperStylePreprocessorTest, CompilesIndentedSass) {
  DeveloperStyleCompileResult result = CompileDeveloperStyleSource(
      {.language = DeveloperStyleLanguage::kSass, .source = R"($brand: #0a84ff
.card
  color: $brand
  .title
    font-weight: 700
)"});
  ASSERT_TRUE(result.succeeded());
  EXPECT_EQ(result.css,
            ".card{color: #0a84ff;}\n"
            ".card .title{font-weight: 700;}\n");
}

TEST(DeveloperStylePreprocessorTest,
     ReportsSyntaxUnsupportedAndResourceBounds) {
  DeveloperStyleCompileResult syntax =
      CompileDeveloperStyleSource({.language = DeveloperStyleLanguage::kSass,
                                   .source = ".card {\n  color: $missing;\n}"});
  EXPECT_EQ(syntax.status, DeveloperStyleCompileStatus::kSyntaxError);
  EXPECT_EQ(syntax.error_line, 2u);
  EXPECT_GT(syntax.error_column, 0u);

  DeveloperStyleCompileResult unsupported = CompileDeveloperStyleSource(
      {.language = DeveloperStyleLanguage::kLess,
       .source = "@import 'filesystem-or-network.less';"});
  EXPECT_EQ(unsupported.status,
            DeveloperStyleCompileStatus::kUnsupportedSyntax);
  EXPECT_TRUE(unsupported.css.empty());

  DeveloperStyleCompileResult oversized = CompileDeveloperStyleSource(
      {.language = DeveloperStyleLanguage::kLess,
       .source = std::string(kMaxDeveloperCssBytes + 1, 'x')});
  EXPECT_EQ(oversized.status, DeveloperStyleCompileStatus::kInvalidRequest);

  std::string selector_product;
  for (int index = 0; index < 65; ++index) {
    if (index != 0) {
      selector_product.push_back(',');
    }
    selector_product.append(".parent-").append(std::to_string(index));
  }
  selector_product.append("{");
  for (int index = 0; index < 65; ++index) {
    if (index != 0) {
      selector_product.push_back(',');
    }
    selector_product.append(".child-").append(std::to_string(index));
  }
  selector_product.append("{color:red;}}");
  DeveloperStyleCompileResult expanded =
      CompileDeveloperStyleSource({.language = DeveloperStyleLanguage::kLess,
                                   .source = std::move(selector_product)});
  EXPECT_EQ(expanded.status, DeveloperStyleCompileStatus::kOutputRejected);
  EXPECT_TRUE(expanded.css.empty());
}

}  // namespace
}  // namespace ahoi
