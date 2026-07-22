#!/usr/bin/env bash
# install-component.sh
# 把构建产物 build/foo_openlyrics.component 拷到 foobar2000 的
# user-components 目录（~/Library/foobar2000-v2/user-components/foo_openlyrics/），
# 供本机 foobar2000 v2.25 启动时扫描加载。
#
# 用法：bash Scripts/install-component.sh [构建目录，默认 build]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${1:-${REPO_ROOT}/build}"

BUNDLE_NAME="foo_openlyrics.component"
SRC_BUNDLE="${BUILD_DIR}/${BUNDLE_NAME}"
DEST_DIR="${HOME}/Library/foobar2000-v2/user-components/foo_openlyrics"
DEST_BUNDLE="${DEST_DIR}/${BUNDLE_NAME}"

if [[ ! -d "${SRC_BUNDLE}" ]]; then
  echo "错误：未找到构建产物 ${SRC_BUNDLE}，请先执行：" >&2
  echo "  /opt/homebrew/bin/cmake --build ${BUILD_DIR} --target foo_openlyrics" >&2
  exit 1
fi

echo "安装 ${SRC_BUNDLE} -> ${DEST_BUNDLE}"
mkdir -p "${DEST_DIR}"
rm -rf "${DEST_BUNDLE}"
cp -R "${SRC_BUNDLE}" "${DEST_BUNDLE}"

echo "校验 codesign..."
codesign --verify --deep --strict "${DEST_BUNDLE}"

echo "安装完成：${DEST_BUNDLE}"
