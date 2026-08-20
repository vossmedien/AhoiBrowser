#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

cd "${REPO_ROOT}"
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
python3 fixtures/http-auth/manage.py run-tests
swift test --package-path spikes/cloudkit --jobs 1 --disable-index-store
