import Foundation
import ImageIO
import UniformTypeIdentifiers
import WebKit

extension MobileBrowserController {
    public func refreshSelectedWebsiteTint() async {
        guard let selectedTabID, let page = selectedPage else { return }
        for delay in [Duration.zero, .milliseconds(180), .milliseconds(420)] {
            if delay != .zero { try? await Task.sleep(for: delay) }
            await sampleWebsiteTint(from: page, tabID: selectedTabID)
            if tabs.first(where: { $0.id == selectedTabID })?.websiteTintARGB != nil {
                return
            }
        }
    }

    public func refreshSelectedFavicon() async {
        guard let tabID = selectedTabID,
              let page = selectedPage,
              !page.isLoading,
              let pageURL = page.url,
              (try? MobileBrowserInputRouter.validateWebURL(pageURL)) != nil else {
            return
        }
        let documentKey = pageURL.absoluteString
        guard faviconFetchInFlight[tabID] != documentKey,
              faviconAttemptedDocumentURLs[tabID] != documentKey else {
            return
        }
        faviconFetchInFlight[tabID] = documentKey
        defer {
            if faviconFetchInFlight[tabID] == documentKey {
                faviconFetchInFlight.removeValue(forKey: tabID)
                faviconAttemptedDocumentURLs[tabID] = documentKey
            }
        }
        let script = #"""
        return await (async () => {
          const declared = document.querySelector(
            'link[rel~="icon"], link[rel="shortcut icon"], link[rel="apple-touch-icon"]'
          )?.href;
          let candidate;
          try {
            candidate = new URL(declared || '/favicon.ico', document.baseURI);
          } catch (_) {
            return null;
          }
          if (candidate.protocol !== 'https:' && candidate.protocol !== 'http:') {
            return null;
          }

          let response;
          try {
            response = await fetch(candidate.href, {
              cache: 'force-cache',
              credentials: 'include',
              redirect: 'follow',
              headers: {
                Accept: 'image/avif,image/webp,image/png,image/jpeg,image/x-icon,image/vnd.microsoft.icon'
              }
            });
          } catch (_) {
            return null;
          }
          if (!response.ok || !response.body) return null;

          let finalURL;
          try { finalURL = new URL(response.url); }
          catch (_) { return null; }
          if (finalURL.protocol !== 'https:' && finalURL.protocol !== 'http:') {
            return null;
          }

          const mime = (response.headers.get('Content-Type') || '')
            .split(';', 1)[0]
            .trim()
            .toLowerCase();
          if (!allowedMIMETypes.includes(mime)) return null;
          const declaredLength = Number(response.headers.get('Content-Length'));
          if (Number.isFinite(declaredLength) && declaredLength > maximumBytes) {
            return null;
          }

          const reader = response.body.getReader();
          const chunks = [];
          let total = 0;
          try {
            while (true) {
              const { done, value } = await reader.read();
              if (done) break;
              if (!(value instanceof Uint8Array)) return null;
              total += value.byteLength;
              if (total > maximumBytes) {
                await reader.cancel();
                return null;
              }
              chunks.push(value);
            }
          } catch (_) {
            return null;
          }
          if (total <= 0) return null;

          const bytes = new Uint8Array(total);
          let cursor = 0;
          for (const chunk of chunks) {
            bytes.set(chunk, cursor);
            cursor += chunk.byteLength;
          }
          let binary = '';
          for (let offset = 0; offset < bytes.length; offset += 0x8000) {
            binary += String.fromCharCode(...bytes.subarray(offset, offset + 0x8000));
          }
          const base64 = btoa(binary);
          const maximumEncodedLength = Math.ceil(maximumBytes / 3) * 4;
          if (base64.length > maximumEncodedLength) return null;
          return {
            base64,
            documentURL: location.href,
            mime,
          };
        })();
        """#
        let value: Any?
        do {
            value = try await page.callJavaScript(
                script,
                arguments: [
                    "allowedMIMETypes": Array(Self.allowedFaviconMIMETypes),
                    "maximumBytes": Self.maximumFaviconBytes,
                ],
                contentWorld: Self.faviconContentWorld
            )
        } catch {
            return
        }
        guard pages[tabID] === page,
              let result = value as? [String: Any],
              let base64 = result["base64"] as? String,
              let mime = result["mime"] as? String,
              let documentURLValue = result["documentURL"] as? String,
              let documentURL = URL(string: documentURLValue),
              (try? MobileBrowserInputRouter.validateWebURL(documentURL)) != nil,
              page.url == documentURL,
              let data = Self.validatedFaviconData(
                base64: base64,
                mime: mime
              ),
              let index = tabs.firstIndex(where: { $0.id == tabID }),
              tabs[index].faviconData != data else { return }
        tabs[index].faviconData = data
        persistSoon()
    }

    func sampleWebsiteTint(from page: WebPage, tabID: UUID) async {
        guard let index = tabs.firstIndex(where: { $0.id == tabID }),
              tabs[index].mode == .normal else { return }
        let documentGeneration = navigationDocumentGenerations[tabID, default: 0]
        let documentURL = page.url
        let declaredValue: Any? = try? await page.callJavaScript(
            "return document.querySelector('meta[name=\"theme-color\"]')?.content || '';"
        )
        let declaredColor = (declaredValue as? String)
            .flatMap(Self.argbColor(from:))
            .flatMap { Self.isEligibleWebsiteTint($0) ? $0 : nil }
        let script = #"""
        return (() => {
          const parse = (value) => {
            if (!value || value === 'transparent' || value === 'auto') return null;
            const direct = value.trim().match(/^#([0-9a-f]{6})$/i);
            if (direct) {
              return [0, 2, 4].map(offset => parseInt(direct[1].slice(offset, offset + 2), 16));
            }
            const probe = document.createElement('span');
            probe.style.cssText = 'position:fixed;left:-10000px;visibility:hidden';
            probe.style.color = value;
            if (!probe.style.color) return null;
            document.documentElement.appendChild(probe);
            const rendered = getComputedStyle(probe).color;
            probe.remove();
            const match = rendered.match(/rgba?\(\s*([\d.]+)[, ]+\s*([\d.]+)[, ]+\s*([\d.]+)(?:\s*[,/]\s*([\d.]+))?/i);
            if (!match || (match[4] !== undefined && Number(match[4]) < 0.35)) return null;
            return [Number(match[1]), Number(match[2]), Number(match[3])];
          };
          const eligible = (rgb) => {
            const [r, g, b] = rgb.map(v => v / 255);
            const max = Math.max(r, g, b), min = Math.min(r, g, b);
            const lightness = (max + min) / 2;
            const saturation = max === min ? 0 : (max - min) / (1 - Math.abs(2 * lightness - 1));
            return lightness > 0.10 && lightness < 0.90 && saturation > 0.16;
          };
          const banner = document.querySelector('header, nav, [role="banner"]');
          const link = document.querySelector('a');
          const candidates = [
            document.querySelector('meta[name="theme-color"]')?.content,
            getComputedStyle(document.documentElement).accentColor,
            banner && getComputedStyle(banner).backgroundColor,
            getComputedStyle(document.body).backgroundColor,
            getComputedStyle(document.documentElement).backgroundColor,
            link && getComputedStyle(link).color
          ];
          for (const candidate of candidates) {
            const rgb = parse(candidate);
            if (!rgb || !eligible(rgb)) continue;
            return '#' + rgb.map(v => Math.round(v).toString(16).padStart(2, '0')).join('');
          }
          return null;
        })()
        """#
        let sampledValue: Any? = try? await page.callJavaScript(script)
        let sampled = sampledValue as? String
        guard let currentIndex = tabs.firstIndex(where: { $0.id == tabID }),
              tabs[currentIndex].mode == .normal,
              pages[tabID] === page,
              page.url == documentURL,
              navigationDocumentGenerations[tabID, default: 0] == documentGeneration else {
            return
        }
        let color = declaredColor ?? sampled.flatMap(Self.argbColor(from:))
        guard tabs[currentIndex].websiteTintARGB != color else { return }
        tabs[currentIndex].websiteTintARGB = color
        persistSoon()
    }

    private static func argbColor(from hexadecimal: String) -> UInt32? {
        let value = hexadecimal.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        guard value.count == 6, let rgb = UInt32(value, radix: 16) else { return nil }
        return 0xFF00_0000 | rgb
    }

    private static func isEligibleWebsiteTint(_ argb: UInt32) -> Bool {
        let channels = [
            Double((argb >> 16) & 0xFF) / 255,
            Double((argb >> 8) & 0xFF) / 255,
            Double(argb & 0xFF) / 255,
        ]
        guard let maximum = channels.max(), let minimum = channels.min() else {
            return false
        }
        let lightness = (maximum + minimum) / 2
        let saturation = maximum == minimum
            ? 0
            : (maximum - minimum) / (1 - abs(2 * lightness - 1))
        return lightness > 0.10 && lightness < 0.90 && saturation > 0.16
    }

    private static func validatedFaviconData(
        base64: String,
        mime: String
    ) -> Data? {
        let maximumEncodedLength = ((maximumFaviconBytes + 2) / 3) * 4
        guard base64.utf8.count <= maximumEncodedLength,
              allowedFaviconMIMETypes.contains(mime.lowercased()),
              let data = Data(base64Encoded: base64),
              !data.isEmpty,
              data.count <= maximumFaviconBytes,
              let source = CGImageSourceCreateWithData(data as CFData, [
                kCGImageSourceShouldCache: false,
              ] as CFDictionary) else {
            return nil
        }
        let imageCount = CGImageSourceGetCount(source)
        guard imageCount > 0, imageCount <= 16 else { return nil }
        for index in 0..<imageCount {
            guard let properties = CGImageSourceCopyPropertiesAtIndex(
                source,
                index,
                [kCGImageSourceShouldCache: false] as CFDictionary
            ) as? [CFString: Any],
                  let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
                  let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
                  width > 0,
                  height > 0,
                  width <= maximumFaviconDimension,
                  height <= maximumFaviconDimension,
                  width * height <= maximumFaviconPixels else {
                return nil
            }
        }
        let thumbnailOptions: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceShouldCacheImmediately: true,
            kCGImageSourceThumbnailMaxPixelSize: persistedFaviconDimension,
        ]
        guard let thumbnail = CGImageSourceCreateThumbnailAtIndex(
            source,
            0,
            thumbnailOptions as CFDictionary
        ) else {
            return nil
        }
        let normalized = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(
            normalized,
            UTType.png.identifier as CFString,
            1,
            nil
        ) else {
            return nil
        }
        CGImageDestinationAddImage(destination, thumbnail, nil)
        guard CGImageDestinationFinalize(destination) else { return nil }
        let normalizedData = normalized as Data
        guard !normalizedData.isEmpty,
              normalizedData.count <= maximumFaviconBytes else {
            return nil
        }
        return normalizedData
    }

}
