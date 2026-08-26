// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ahoi/browser/developer_toolkit/developer_main_world_executor.h"

#include <string>
#include <utility>

#include "ahoi/browser/developer_toolkit/developer_profile_types.h"
#include "base/containers/span.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace ahoi {
namespace {

class MainWorldRequest final : public content::DevToolsAgentHostClient {
 public:
  MainWorldRequest(content::WebContents* web_contents, std::string source)
      : expected_origin_(url::Origin::Create(
            web_contents ? web_contents->GetLastCommittedURL() : GURL())),
        source_(std::move(source)) {}

  MainWorldRequest(const MainWorldRequest&) = delete;
  MainWorldRequest& operator=(const MainWorldRequest&) = delete;

  bool Start(content::WebContents* web_contents) {
    base::DictValue command;
    command.Set("id", kRequestId);
    command.Set("method", "Runtime.evaluate");
    command.Set("params", base::DictValue()
                              .Set("expression", std::move(source_))
                              .Set("silent", true)
                              .Set("includeCommandLineAPI", false)
                              .Set("userGesture", false)
                              .Set("awaitPromise", false)
                              .Set("returnByValue", false));
    std::string message;
    if (!base::JSONWriter::Write(command, &message)) {
      return false;
    }
    agent_host_ = content::DevToolsAgentHost::GetOrCreateFor(web_contents);
    if (!agent_host_ || !agent_host_->AttachClient(this)) {
      agent_host_.reset();
      return false;
    }
    agent_host_->DispatchProtocolMessage(this, base::as_byte_span(message));
    return true;
  }

  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override {
    if (agent_host != agent_host_.get()) {
      return;
    }
    std::optional<base::Value> response = base::JSONReader::Read(
        base::as_string_view(message), base::JSON_REPLACE_INVALID_CHARACTERS);
    if (response && response->is_dict() &&
        response->GetDict().FindInt("id") == kRequestId) {
      DetachAndDelete();
    }
  }

  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override {
    if (agent_host == agent_host_.get()) {
      agent_host_.reset();
      delete this;
    }
  }

  bool MayAttachToURL(const GURL& url, bool /*is_webui*/) override {
    return url.SchemeIsHTTPOrHTTPS() &&
           url::Origin::Create(url) == expected_origin_;
  }

  std::string GetTypeForMetrics() override { return "Other"; }

  ~MainWorldRequest() override = default;

 private:
  void DetachAndDelete() {
    scoped_refptr<content::DevToolsAgentHost> host = std::move(agent_host_);
    if (host) {
      host->DetachClient(this);
    }
    delete this;
  }

  static constexpr int kRequestId = 1;
  const url::Origin expected_origin_;
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  std::string source_;
};

}  // namespace

bool ExecuteDeveloperJavaScriptInMainWorld(content::WebContents* web_contents,
                                           std::string_view source) {
  if (!web_contents || source.empty() ||
      source.size() > kMaxDeveloperJavaScriptBytes ||
      !base::IsStringUTF8(source) ||
      source.find('\0') != std::string_view::npos ||
      !web_contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  auto* request = new MainWorldRequest(web_contents, std::string(source));
  if (!request->Start(web_contents)) {
    delete request;
    return false;
  }
  return true;
}

}  // namespace ahoi
