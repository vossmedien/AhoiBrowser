# Phase-0 branding overlay

The GN profiles point `branding_file_path` here while intentionally retaining
Chromium's open-source theme assets for the first control-derived build. This
changes product/app/installer names and the provisional bundle identifier
without enabling Google Chrome branding or internal resources.

The bundle identifier is not a release claim. It must be finalized and registered
with the Apple team before CloudKit/signing profiles are created. Product icons
will replace the temporary Chromium artwork through Ahoi-owned assets after the
native app builds and launches; no Chrome trademark assets are used.
