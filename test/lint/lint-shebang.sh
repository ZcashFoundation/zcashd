#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Copyright (c) 2020-2022 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://www.opensource.org/licenses/mit-license.php .
#
# Assert expected shebang lines

export LC_ALL=C

KNOWN_LEGACY_PYTHON_SHEBANGS=(
    "contrib/devtools/security-check.py"
    "contrib/devtools/symbol-check.py"
    "contrib/linearize/linearize-data.py"
    "contrib/linearize/linearize-hashes.py"
    "contrib/seeds/generate-seeds.py"
    "contrib/seeds/makeseeds.py"
    "contrib/testgen/gen_base58_test_vectors.py"
    "contrib/zmq/zmq_sub.py"
    "share/rpcuser/rpcuser.py"
)

KNOWN_LEGACY_SHELL_SHEBANGS=(
    "contrib/devtools/github-merge.sh"
)

is_known_legacy_file() {
    local file="$1"
    shift
    local known_file
    for known_file in "$@"; do
        if [[ "${file}" == "${known_file}" ]]; then
            return 0
        fi
    done
    return 1
}

EXIT_CODE=0
for PYTHON_FILE in $(git ls-files -- "*.py"); do
    if is_known_legacy_file "${PYTHON_FILE}" "${KNOWN_LEGACY_PYTHON_SHEBANGS[@]}"; then
        continue
    fi
    if [[ $(head -c 2 "${PYTHON_FILE}") == "#!" &&
          $(head -n 1 "${PYTHON_FILE}") != "#!/usr/bin/env python3" ]]; then
        echo "Missing shebang \"#!/usr/bin/env python3\" in ${PYTHON_FILE} (do not use python or python2)"
        EXIT_CODE=1
    fi
done
for SHELL_FILE in $(git ls-files -- "*.sh"); do
    if is_known_legacy_file "${SHELL_FILE}" "${KNOWN_LEGACY_SHELL_SHEBANGS[@]}"; then
        continue
    fi
    if [[ $(head -n 1 "${SHELL_FILE}") != "#!/usr/bin/env bash" &&
          $(head -n 1 "${SHELL_FILE}") != "#!/bin/sh" ]]; then
        echo "Missing expected shebang \"#!/usr/bin/env bash\" or \"#!/bin/sh\" in ${SHELL_FILE}"
        EXIT_CODE=1
    fi
done
exit ${EXIT_CODE}
