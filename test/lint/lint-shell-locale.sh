#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2020-2022 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
#
# Make sure all shell scripts:
# a.) explicitly opt out of locale dependence using
#     "export LC_ALL=C" or "export LC_ALL=C.UTF-8", or
# b.) explicitly opt in to locale dependence using the annotation below.

export LC_ALL=C

KNOWN_LEGACY_LOCALE_SCRIPTS=(
    "contrib/devtools/fix-copyright-headers.sh"
    "contrib/devtools/github-merge.sh"
    "contrib/devtools/split-debug.sh"
    "qa/rpc-tests/test-delta-corruption.sh"
    "src/secp256k1/autogen.sh"
    "src/secp256k1/ci/ci.sh"
    "src/secp256k1/tools/check-abi.sh"
    "src/univalue/autogen.sh"
)

is_known_legacy_locale_script() {
    local file="$1"
    local known_file
    for known_file in "${KNOWN_LEGACY_LOCALE_SCRIPTS[@]}"; do
        if [[ "${file}" == "${known_file}" ]]; then
            return 0
        fi
    done
    return 1
}

EXIT_CODE=0
for SHELL_SCRIPT in $(git ls-files -- "*.sh"); do
    if is_known_legacy_locale_script "${SHELL_SCRIPT}"; then
        continue
    fi
    if grep -q "# This script is intentionally locale-dependent by not setting \"export LC_ALL=C\"." "${SHELL_SCRIPT}"; then
        continue
    fi
    FIRST_NON_COMMENT_LINE=$(grep -vE '^(#.*)?$' "${SHELL_SCRIPT}" | head -1)
    if [[ ${FIRST_NON_COMMENT_LINE} != "export LC_ALL=C" && ${FIRST_NON_COMMENT_LINE} != "export LC_ALL=\"C\"" && ${FIRST_NON_COMMENT_LINE} != "export LC_ALL=C.UTF-8" ]]; then
        echo "Missing \"export LC_ALL=C\" (to avoid locale dependence) as first non-comment non-empty line in ${SHELL_SCRIPT}"
        EXIT_CODE=1
    fi
done
exit ${EXIT_CODE}
