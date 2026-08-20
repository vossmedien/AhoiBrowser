# Contributing

AhoiBrowser accepts focused changes that preserve the product contract and keep
the Chromium delta small enough to rebase quickly after security releases.

## Required workflow

1. Read `docs/PRODUCT_PRINCIPLES.md`, `docs/ARCHITECTURE.md`, and the relevant
   decision records.
2. Add or update a test whose identifier maps to the master target matrix.
3. Keep Chromium modifications in documented, independently applicable patches
   or in the Ahoi-owned overlay target.
4. Run `./scripts/test-repository.sh` before submitting.
5. Use `git commit -s` so every commit contains a DCO sign-off.
6. Include evidence for user-visible behavior. A screenshot alone is not proof
   of process, sandbox, persistence, sync, or security behavior.

Do not commit credentials, cookies, profiles, browser databases, certificate
exports, provisioning profiles, notarization tokens, or real customer data.
CI independently enforces ShellCheck, actionlint, secret scanning, and a DCO
trailer matching each non-merge commit author.

## Developer Certificate of Origin

By adding `Signed-off-by: Name <email>` to a commit, a contributor certifies the
Developer Certificate of Origin 1.1: <https://developercertificate.org/>.
