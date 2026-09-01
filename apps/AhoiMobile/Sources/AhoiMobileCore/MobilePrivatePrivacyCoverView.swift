import SwiftUI

struct MobilePrivatePrivacyCoverView: View {
    let accentTint: Color

    private var title: String {
        CompanionL10n.string(
            "browser.private.cover.title",
            fallback: "Private browsing protected"
        )
    }

    private var message: String {
        CompanionL10n.string(
            "browser.private.cover.message",
            fallback: "Return to AhoiBrowser to view this private tab."
        )
    }

    var body: some View {
        ZStack {
            MobileBrowserChromeTheme.privateBackground
                .ignoresSafeArea()
            VStack(spacing: 12) {
                Image(systemName: "hand.raised.fill")
                    .font(.system(size: 34, weight: .semibold))
                    .foregroundStyle(accentTint)
                Text(title)
                    .font(.headline)
                    .foregroundStyle(.white)
                Text(message)
                    .font(.subheadline)
                    .foregroundStyle(.white.opacity(0.72))
                    .multilineTextAlignment(.center)
            }
            .padding(28)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("browser.private-privacy-cover")
        .background {
            MobilePrivateSceneShield(title: title, message: message)
        }
    }
}
