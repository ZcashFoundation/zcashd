// Copyright (c) 2017-2023 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZCASH_DEPRECATION_H
#define ZCASH_DEPRECATION_H

#include "config/bitcoin-config.h"

#include <optional>
#include <set>
#include <string>

// Used by offline transaction signing to estimate the current consensus branch.
static const int APPROX_RELEASE_HEIGHT = 3360652;

//! Defaults for -allowdeprecated
static const std::set<std::string> DEFAULT_ALLOW_DEPRECATED{{
    // Node-level features
    "createrawtransaction",
    "signrawtransaction",
    "getnetworkhashps",

    // Wallet-level features
#ifdef ENABLE_WALLET
    "z_gettotalbalance",
    "fundrawtransaction",
    "keypoolrefill",
    "settxfee",
#endif
}};
static const std::set<std::string> DEFAULT_DENY_DEPRECATED{{
    // Node-level features
    "gbt_oldhashes",
    "addrtype",

    // Wallet-level features
#ifdef ENABLE_WALLET
    "getnewaddress",
    "getrawchangeaddress",
    "z_getnewaddress",
    "z_getbalance",
    "z_listaddresses",
    "legacy_privacy",
    "wallettxvjoinsplit",
#endif
}};

// Flags that enable deprecated functionality.
extern bool fEnableGbtOldHashes;
extern bool fEnableAddrTypeField;
extern bool fEnableGetNetworkHashPS;
extern bool fEnableCreateRawTransaction;
extern bool fEnableSignRawTransaction;
#ifdef ENABLE_WALLET
extern bool fEnableGetNewAddress;
extern bool fEnableGetRawChangeAddress;
extern bool fEnableZGetNewAddress;
extern bool fEnableZGetBalance;
extern bool fEnableZGetTotalBalance;
extern bool fEnableZListAddresses;
extern bool fEnableLegacyPrivacyStrategy;
extern bool fEnableWalletTxVJoinSplit;
extern bool fEnableFundRawTransaction;
extern bool fEnableKeyPoolRefill;
extern bool fEnableSetTxFee;
#endif

/**
 * Checks config options for enabling and/or disabling of deprecated
 * features and sets flags that enable deprecated features accordingly.
 *
 * @return std::nullopt if successful, or an error message indicating what
 * values are permitted for `-allowdeprecated`.
 */
std::optional<std::string> LoadAllowedDeprecatedFeatures();

/**
 * Returns a comma-separated list of the valid arguments to the -allowdeprecated
 * CLI option.
 */
std::string GetAllowableDeprecatedFeatures();

/**
 * Returns a string to be included in the help text of a deprecated RPC method.
 */
std::string Deprecated(bool enabled, std::string method, std::string instead);

#endif // ZCASH_DEPRECATION_H
