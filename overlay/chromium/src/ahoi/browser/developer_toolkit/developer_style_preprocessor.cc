// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_style_preprocessor.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace ahoi {
namespace {

constexpr size_t kMaxSyntaxNodes = 4096;
constexpr size_t kMaxNestingDepth = 32;
constexpr size_t kMaxVariableExpansions = 64;

struct PositionedText {
  std::string text;
  uint32_t line = 1;
  uint32_t column = 1;
};

struct RuleNode {
  PositionedText header;
  std::vector<PositionedText> declarations;
  std::vector<RuleNode> children;
};

DeveloperStyleCompileResult Failure(DeveloperStyleCompileStatus status,
                                    uint32_t line,
                                    uint32_t column) {
  return {.status = status, .error_line = line, .error_column = column};
}

bool IsIdentifierStart(char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  return std::isalpha(byte) || value == '_';
}

bool IsIdentifierPart(char value) {
  const unsigned char byte = static_cast<unsigned char>(value);
  return std::isalnum(byte) || value == '_' || value == '-';
}

std::string Trim(std::string_view value) {
  std::string_view trimmed = base::TrimWhitespaceASCII(value, base::TRIM_ALL);
  return std::string(trimmed);
}

bool StartsWithForbiddenDirective(std::string_view value) {
  value = base::TrimWhitespaceASCII(value, base::TRIM_LEADING);
  constexpr std::string_view kForbidden[] = {"@import", "@use", "@forward",
                                             "@plugin", "@require"};
  for (std::string_view directive : kForbidden) {
    if (base::StartsWith(value, directive,
                         base::CompareCase::INSENSITIVE_ASCII) &&
        (value.size() == directive.size() ||
         !IsIdentifierPart(value[directive.size()]))) {
      return true;
    }
  }
  return false;
}

bool IsIndentedSassDeclaration(std::string_view line) {
  line = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
  if (line.empty() || line.front() == '$') {
    return true;
  }
  if (line.front() == '&' || line.front() == '.' || line.front() == '#' ||
      line.front() == '[' || line.front() == ':' || line.front() == '>' ||
      line.front() == '+' || line.front() == '~' || line.front() == '*') {
    return false;
  }
  if (line.front() == '@') {
    return base::StartsWith(line, "@include", base::CompareCase::SENSITIVE) ||
           base::StartsWith(line, "@extend", base::CompareCase::SENSITIVE) ||
           base::StartsWith(line, "@debug", base::CompareCase::SENSITIVE) ||
           base::StartsWith(line, "@warn", base::CompareCase::SENSITIVE) ||
           base::StartsWith(line, "@error", base::CompareCase::SENSITIVE);
  }
  const size_t colon = line.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return false;
  }
  if (!std::all_of(line.begin(), line.begin() + colon, IsIdentifierPart)) {
    return false;
  }
  return colon + 1 == line.size() || base::IsAsciiWhitespace(line[colon + 1]);
}

std::optional<std::string> ConvertIndentedSass(std::string_view source,
                                               uint32_t* error_line,
                                               uint32_t* error_column) {
  std::vector<std::string_view> lines = base::SplitStringPiece(
      source, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  struct Frame {
    size_t indent;
  };
  std::vector<Frame> frames;
  std::string result;
  result.reserve(source.size() + lines.size() * 3);
  for (size_t index = 0; index < lines.size(); ++index) {
    std::string_view raw = lines[index];
    if (!raw.empty() && raw.back() == '\r') {
      raw.remove_suffix(1);
    }
    size_t indent = 0;
    while (indent < raw.size() && raw[indent] == ' ') {
      ++indent;
    }
    if (indent < raw.size() && raw[indent] == '\t') {
      *error_line = static_cast<uint32_t>(index + 1);
      *error_column = static_cast<uint32_t>(indent + 1);
      return std::nullopt;
    }
    std::string line = Trim(raw.substr(indent));
    if (line.empty() || base::StartsWith(line, "//")) {
      result.push_back('\n');
      continue;
    }
    if (frames.empty() && indent != 0) {
      *error_line = static_cast<uint32_t>(index + 1);
      *error_column = 1;
      return std::nullopt;
    }
    while (!frames.empty() && indent <= frames.back().indent) {
      result.append("}\n");
      frames.pop_back();
    }
    if (!frames.empty() && indent <= frames.back().indent) {
      *error_line = static_cast<uint32_t>(index + 1);
      *error_column = static_cast<uint32_t>(indent + 1);
      return std::nullopt;
    }
    result.append(line);
    if (IsIndentedSassDeclaration(line)) {
      result.append(";\n");
    } else {
      result.append(" {\n");
      frames.push_back({indent});
    }
    if (result.size() > kMaxDeveloperCssBytes * 2) {
      *error_line = static_cast<uint32_t>(index + 1);
      *error_column = 1;
      return std::nullopt;
    }
  }
  while (!frames.empty()) {
    result.append("}\n");
    frames.pop_back();
  }
  return result;
}

class Parser {
 public:
  Parser(DeveloperStyleLanguage language, std::string source)
      : language_(language), source_(std::move(source)) {}

  DeveloperStyleCompileResult Compile() {
    RuleNode root;
    if (!ParseBlock(root, /*expects_closing_brace=*/false, 0)) {
      return error_;
    }
    std::string css;
    for (const RuleNode& child : root.children) {
      if (!EmitRule(child, {}, &css, 0)) {
        return error_;
      }
    }
    if (root.children.empty() || css.empty()) {
      return Failure(DeveloperStyleCompileStatus::kSyntaxError, 1, 1);
    }
    return {.status = DeveloperStyleCompileStatus::kSucceeded,
            .css = std::move(css)};
  }

 private:
  bool ParseBlock(RuleNode& parent, bool expects_closing_brace, size_t depth) {
    if (depth > kMaxNestingDepth) {
      return SetError(DeveloperStyleCompileStatus::kInvalidRequest, line_,
                      column_);
    }
    while (true) {
      if (!SkipWhitespaceAndComments()) {
        return false;
      }
      if (position_ == source_.size()) {
        return !expects_closing_brace ||
               SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                        column_);
      }
      if (source_[position_] == '}') {
        if (!expects_closing_brace) {
          return SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                          column_);
        }
        Advance(source_[position_]);
        return true;
      }
      PositionedText chunk;
      char delimiter = '\0';
      if (!ReadChunk(&chunk, &delimiter)) {
        return false;
      }
      chunk.text = Trim(chunk.text);
      if (delimiter == '{') {
        if (chunk.text.empty() || StartsWithForbiddenDirective(chunk.text)) {
          return SetError(chunk.text.empty()
                              ? DeveloperStyleCompileStatus::kSyntaxError
                              : DeveloperStyleCompileStatus::kUnsupportedSyntax,
                          chunk.line, chunk.column);
        }
        if (++node_count_ > kMaxSyntaxNodes) {
          return SetError(DeveloperStyleCompileStatus::kInvalidRequest,
                          chunk.line, chunk.column);
        }
        RuleNode child;
        child.header = std::move(chunk);
        if (!ParseBlock(child, /*expects_closing_brace=*/true, depth + 1)) {
          return false;
        }
        parent.children.push_back(std::move(child));
        continue;
      }
      if (!chunk.text.empty() && !ConsumeStatement(parent, std::move(chunk))) {
        return false;
      }
      if (delimiter == '}') {
        if (!expects_closing_brace) {
          return SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                          column_);
        }
        return true;
      }
      if (delimiter == '\0') {
        return !expects_closing_brace ||
               SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                        column_);
      }
    }
  }

  bool SkipWhitespaceAndComments() {
    while (position_ < source_.size()) {
      if (base::IsAsciiWhitespace(source_[position_])) {
        Advance(source_[position_]);
        continue;
      }
      if (position_ + 1 < source_.size() && source_[position_] == '/' &&
          source_[position_ + 1] == '*') {
        Advance('/');
        Advance('*');
        bool closed = false;
        while (position_ + 1 < source_.size()) {
          if (source_[position_] == '*' && source_[position_ + 1] == '/') {
            Advance('*');
            Advance('/');
            closed = true;
            break;
          }
          Advance(source_[position_]);
        }
        if (!closed) {
          return SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                          column_);
        }
        continue;
      }
      if (position_ + 1 < source_.size() && source_[position_] == '/' &&
          source_[position_ + 1] == '/') {
        while (position_ < source_.size() && source_[position_] != '\n') {
          Advance(source_[position_]);
        }
        continue;
      }
      break;
    }
    return true;
  }

  bool ReadChunk(PositionedText* chunk, char* delimiter) {
    chunk->line = line_;
    chunk->column = column_;
    char quote = '\0';
    bool escaped = false;
    size_t parentheses = 0;
    size_t brackets = 0;
    while (position_ < source_.size()) {
      const char current = source_[position_];
      if (quote != '\0') {
        chunk->text.push_back(current);
        Advance(current);
        if (escaped) {
          escaped = false;
        } else if (current == '\\') {
          escaped = true;
        } else if (current == quote) {
          quote = '\0';
        }
        continue;
      }
      if (current == '\'' || current == '"') {
        quote = current;
        chunk->text.push_back(current);
        Advance(current);
        continue;
      }
      if (current == '(') {
        ++parentheses;
      } else if (current == ')') {
        if (parentheses == 0) {
          return SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                          column_);
        }
        --parentheses;
      } else if (current == '[') {
        ++brackets;
      } else if (current == ']') {
        if (brackets == 0) {
          return SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                          column_);
        }
        --brackets;
      }
      if (parentheses == 0 && brackets == 0 &&
          (current == '{' || current == ';' || current == '}')) {
        *delimiter = current;
        Advance(current);
        return true;
      }
      chunk->text.push_back(current);
      Advance(current);
    }
    if (quote != '\0' || parentheses != 0 || brackets != 0) {
      return SetError(DeveloperStyleCompileStatus::kSyntaxError, line_,
                      column_);
    }
    *delimiter = '\0';
    return true;
  }

  bool ConsumeStatement(RuleNode& parent, PositionedText statement) {
    if (++node_count_ > kMaxSyntaxNodes) {
      return SetError(DeveloperStyleCompileStatus::kInvalidRequest,
                      statement.line, statement.column);
    }
    if (StartsWithForbiddenDirective(statement.text)) {
      return SetError(DeveloperStyleCompileStatus::kUnsupportedSyntax,
                      statement.line, statement.column);
    }
    const char variable_prefix =
        language_ == DeveloperStyleLanguage::kLess ? '@' : '$';
    if (!statement.text.empty() && statement.text.front() == variable_prefix) {
      size_t cursor = 1;
      if (cursor < statement.text.size() &&
          IsIdentifierStart(statement.text[cursor])) {
        while (cursor < statement.text.size() &&
               IsIdentifierPart(statement.text[cursor])) {
          ++cursor;
        }
        size_t separator = cursor;
        while (separator < statement.text.size() &&
               base::IsAsciiWhitespace(statement.text[separator])) {
          ++separator;
        }
        if (separator < statement.text.size() &&
            statement.text[separator] == ':') {
          const std::string name = statement.text.substr(1, cursor - 1);
          const std::string value = Trim(statement.text.substr(separator + 1));
          if (value.empty()) {
            return SetError(DeveloperStyleCompileStatus::kSyntaxError,
                            statement.line, statement.column);
          }
          variables_[name] = value;
          return true;
        }
      }
    }
    if (parent.header.text.empty()) {
      return SetError(DeveloperStyleCompileStatus::kSyntaxError, statement.line,
                      statement.column);
    }
    if (statement.text.find(':') == std::string::npos ||
        base::StartsWith(statement.text, "@include") ||
        base::StartsWith(statement.text, "@extend") ||
        (language_ == DeveloperStyleLanguage::kLess &&
         statement.text.front() == '.')) {
      return SetError(DeveloperStyleCompileStatus::kUnsupportedSyntax,
                      statement.line, statement.column);
    }
    parent.declarations.push_back(std::move(statement));
    return true;
  }

  std::optional<std::string> ExpandVariables(const PositionedText& input) {
    std::set<std::string> stack;
    return ExpandVariablesImpl(input.text, input.line, input.column, &stack, 0);
  }

  std::optional<std::string> ExpandVariablesImpl(std::string_view input,
                                                 uint32_t line,
                                                 uint32_t column,
                                                 std::set<std::string>* stack,
                                                 size_t depth) {
    if (depth > kMaxVariableExpansions) {
      SetError(DeveloperStyleCompileStatus::kInvalidRequest, line, column);
      return std::nullopt;
    }
    const char prefix = language_ == DeveloperStyleLanguage::kLess ? '@' : '$';
    std::string result;
    result.reserve(input.size());
    char quote = '\0';
    bool escaped = false;
    for (size_t index = 0; index < input.size();) {
      const char current = input[index];
      if (quote != '\0') {
        result.push_back(current);
        ++index;
        if (escaped) {
          escaped = false;
        } else if (current == '\\') {
          escaped = true;
        } else if (current == quote) {
          quote = '\0';
        }
        continue;
      }
      if (current == '\'' || current == '"') {
        quote = current;
        result.push_back(current);
        ++index;
        continue;
      }
      if (current != prefix || index + 1 >= input.size() ||
          !IsIdentifierStart(input[index + 1])) {
        result.push_back(current);
        ++index;
        continue;
      }
      size_t end = index + 2;
      while (end < input.size() && IsIdentifierPart(input[end])) {
        ++end;
      }
      const std::string name(input.substr(index + 1, end - index - 1));
      const auto found = variables_.find(name);
      if (found == variables_.end()) {
        if (language_ == DeveloperStyleLanguage::kLess && index == 0 &&
            base::StartsWith(input, "@" + name)) {
          result.append(input.substr(index, end - index));
          index = end;
          continue;
        }
        SetError(DeveloperStyleCompileStatus::kSyntaxError, line,
                 column + static_cast<uint32_t>(index));
        return std::nullopt;
      }
      if (!stack->insert(name).second) {
        SetError(DeveloperStyleCompileStatus::kSyntaxError, line,
                 column + static_cast<uint32_t>(index));
        return std::nullopt;
      }
      std::optional<std::string> expanded = ExpandVariablesImpl(
          found->second, line, column + static_cast<uint32_t>(index), stack,
          depth + 1);
      stack->erase(name);
      if (!expanded) {
        return std::nullopt;
      }
      result.append(*expanded);
      index = end;
      if (result.size() > kMaxDeveloperCssBytes) {
        SetError(DeveloperStyleCompileStatus::kOutputRejected, line, column);
        return std::nullopt;
      }
    }
    return result;
  }

  std::vector<std::string> SplitSelectors(std::string_view selectors) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t parentheses = 0;
    size_t brackets = 0;
    for (size_t index = 0; index <= selectors.size(); ++index) {
      const char current = index == selectors.size() ? ',' : selectors[index];
      if (current == '(') {
        ++parentheses;
      } else if (current == ')' && parentheses > 0) {
        --parentheses;
      } else if (current == '[') {
        ++brackets;
      } else if (current == ']' && brackets > 0) {
        --brackets;
      } else if (current == ',' && parentheses == 0 && brackets == 0) {
        std::string selector = Trim(selectors.substr(start, index - start));
        if (!selector.empty()) {
          result.push_back(std::move(selector));
        }
        start = index + 1;
      }
    }
    return result;
  }

  std::optional<std::vector<std::string>> CombineSelectors(
      const std::vector<std::string>& parents,
      const PositionedText& child) {
    std::optional<std::string> expanded = ExpandVariables(child);
    if (!expanded) {
      return std::nullopt;
    }
    std::vector<std::string> children = SplitSelectors(*expanded);
    if (children.empty()) {
      SetError(DeveloperStyleCompileStatus::kSyntaxError, child.line,
               child.column);
      return std::nullopt;
    }
    if (parents.empty()) {
      return children;
    }
    std::vector<std::string> combined;
    if (parents.size() * children.size() > kMaxSyntaxNodes) {
      SetError(DeveloperStyleCompileStatus::kOutputRejected, child.line,
               child.column);
      return std::nullopt;
    }
    for (const std::string& parent : parents) {
      for (const std::string& selector : children) {
        std::string value;
        if (selector.find('&') != std::string::npos) {
          value = selector;
          base::ReplaceSubstringsAfterOffset(&value, 0, "&", parent);
        } else {
          value = parent + " " + selector;
        }
        combined.push_back(std::move(value));
      }
    }
    return combined;
  }

  bool Append(std::string_view value,
              std::string* output,
              const PositionedText& source) {
    if (output->size() + value.size() > kMaxDeveloperCssBytes) {
      return SetError(DeveloperStyleCompileStatus::kOutputRejected, source.line,
                      source.column);
    }
    output->append(value);
    return true;
  }

  bool EmitDeclarations(const RuleNode& rule,
                        const std::vector<std::string>& selectors,
                        std::string* output) {
    if (rule.declarations.empty()) {
      return true;
    }
    if (!selectors.empty()) {
      for (size_t index = 0; index < selectors.size(); ++index) {
        if (index != 0 && !Append(",", output, rule.header)) {
          return false;
        }
        if (!Append(selectors[index], output, rule.header)) {
          return false;
        }
      }
      if (!Append("{", output, rule.header)) {
        return false;
      }
    }
    for (const PositionedText& declaration : rule.declarations) {
      std::optional<std::string> expanded = ExpandVariables(declaration);
      if (!expanded || !Append(*expanded, output, declaration) ||
          !Append(";", output, declaration)) {
        return false;
      }
    }
    return selectors.empty() || Append("}\n", output, rule.header);
  }

  bool EmitRule(const RuleNode& rule,
                const std::vector<std::string>& parents,
                std::string* output,
                size_t depth) {
    if (depth > kMaxNestingDepth) {
      return SetError(DeveloperStyleCompileStatus::kOutputRejected,
                      rule.header.line, rule.header.column);
    }
    std::optional<std::string> expanded_header = ExpandVariables(rule.header);
    if (!expanded_header) {
      return false;
    }
    if (!expanded_header->empty() && expanded_header->front() == '@') {
      if (StartsWithForbiddenDirective(*expanded_header)) {
        return SetError(DeveloperStyleCompileStatus::kUnsupportedSyntax,
                        rule.header.line, rule.header.column);
      }
      if (!Append(*expanded_header, output, rule.header) ||
          !Append("{\n", output, rule.header)) {
        return false;
      }
      if (!EmitDeclarations(rule, parents, output)) {
        return false;
      }
      for (const RuleNode& child : rule.children) {
        if (!EmitRule(child, parents, output, depth + 1)) {
          return false;
        }
      }
      return Append("}\n", output, rule.header);
    }
    std::optional<std::vector<std::string>> selectors =
        CombineSelectors(parents, rule.header);
    if (!selectors || !EmitDeclarations(rule, *selectors, output)) {
      return false;
    }
    for (const RuleNode& child : rule.children) {
      if (!EmitRule(child, *selectors, output, depth + 1)) {
        return false;
      }
    }
    return true;
  }

  void Advance(char value) {
    ++position_;
    if (value == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
  }

  bool SetError(DeveloperStyleCompileStatus status,
                uint32_t line,
                uint32_t column) {
    error_ = Failure(status, line, column);
    return false;
  }

  const DeveloperStyleLanguage language_;
  const std::string source_;
  size_t position_ = 0;
  uint32_t line_ = 1;
  uint32_t column_ = 1;
  size_t node_count_ = 0;
  std::map<std::string, std::string> variables_;
  DeveloperStyleCompileResult error_ =
      Failure(DeveloperStyleCompileStatus::kSyntaxError, 1, 1);
};

}  // namespace

DeveloperStyleCompileResult CompileDeveloperStyleSource(
    DeveloperStyleCompileRequest request) {
  if (request.language == DeveloperStyleLanguage::kCss ||
      request.source.empty() || request.source.size() > kMaxDeveloperCssBytes ||
      !base::IsStringUTF8(request.source) ||
      request.source.find('\0') != std::string::npos) {
    return Failure(DeveloperStyleCompileStatus::kInvalidRequest, 1, 1);
  }
  if (request.language == DeveloperStyleLanguage::kSass &&
      request.source.find('{') == std::string::npos) {
    uint32_t error_line = 1;
    uint32_t error_column = 1;
    std::optional<std::string> converted =
        ConvertIndentedSass(request.source, &error_line, &error_column);
    if (!converted) {
      return Failure(DeveloperStyleCompileStatus::kSyntaxError, error_line,
                     error_column);
    }
    request.source = std::move(*converted);
  }
  return Parser(request.language, std::move(request.source)).Compile();
}

}  // namespace ahoi
