#!/usr/bin/env bash
# Creates a source archive that includes populated submodule contents.
#
# git archive only captures files tracked by the superproject; submodule
# directories appear as empty folders in the resulting tarball.  This script
# appends the populated submodule trees so the archive can be built offline
# without FetchContent or system packages.
#
# Usage:
#   ./tools/make_release_archive.sh [VERSION]
#
# VERSION defaults to the most recent git tag, or "0.0.0" if no tags exist.
# Output: edge-tts-cpp-<VERSION>.tar.gz in the current directory.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

VERSION="${1:-$(git describe --tags --abbrev=0 2>/dev/null || echo "0.0.0")}"
ARCHIVE_NAME="edge-tts-cpp-${VERSION}"
DEST="${ARCHIVE_NAME}.tar.gz"
TMP_DIR="$(mktemp -d)"

cleanup() { rm -rf "${TMP_DIR}"; }
trap cleanup EXIT

echo "Creating source archive ${DEST} (version ${VERSION}) ..."

# 1. Archive the superproject (tracked files only)
git archive --prefix="${ARCHIVE_NAME}/" HEAD | tar x -C "${TMP_DIR}"

# 2. Append each populated submodule
for sm_path in submodules/json submodules/ixwebsocket; do
    if [ -f "${sm_path}/CMakeLists.txt" ]; then
        sm_dest="${TMP_DIR}/${ARCHIVE_NAME}/${sm_path}"
        mkdir -p "${sm_dest}"
        # rsync: copy contents, skip .git metadata to keep the archive clean
        rsync -a --exclude='.git' --exclude='*.pyc' "${sm_path}/" "${sm_dest}/"
        echo "  included submodule: ${sm_path}"
    else
        echo "  WARNING: ${sm_path} is not populated — skipping (run: git submodule update --init ${sm_path})"
    fi
done

# 3. Pack
tar czf "${DEST}" -C "${TMP_DIR}" "${ARCHIVE_NAME}"
echo "Done: ${DEST}"
