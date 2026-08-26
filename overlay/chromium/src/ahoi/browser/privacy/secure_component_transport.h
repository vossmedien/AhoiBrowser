// Copyright 2026 The AhoiBrowser Authors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef AHOI_BROWSER_PRIVACY_SECURE_COMPONENT_TRANSPORT_H_
#define AHOI_BROWSER_PRIVACY_SECURE_COMPONENT_TRANSPORT_H_

#include "base/memory/scoped_refptr.h"

class GURL;

namespace update_client {
class NetworkFetcherFactory;
}

namespace ahoi::privacy {

// Component metadata is signed, but AhoiBrowser still refuses plaintext
// metadata and payload transport. This keeps endpoint policy independently
// auditable and prevents request metadata from crossing an unencrypted hop.
bool IsSecureComponentTransportUrl(const GURL& url);

scoped_refptr<update_client::NetworkFetcherFactory>
WrapSecureComponentNetworkFetcherFactory(
    scoped_refptr<update_client::NetworkFetcherFactory> delegate);

}  // namespace ahoi::privacy

#endif  // AHOI_BROWSER_PRIVACY_SECURE_COMPONENT_TRANSPORT_H_
