// Copyright (c) 2017-2025 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "deprecation.h"

#include "util/system.h"

// Flags that enable deprecated functionality.
bool fEnableGbtOldHashes = true;
bool fEnableAddrTypeField = true;
bool fEnableGetNetworkHashPS = true;
bool fEnableCreateRawTransaction = true;
bool fEnableSignRawTransaction = true;
#ifdef ENABLE_WALLET
bool fEnableGetNewAddress = true;
bool fEnableGetRawChangeAddress = true;
bool fEnableZGetNewAddress = true;
bool fEnableZGetBalance = true;
bool fEnableZGetTotalBalance = true;
bool fEnableZListAddresses = true;
bool fEnableLegacyPrivacyStrategy = true;
bool fEnableWalletTxVJoinSplit = true;
bool fEnableFundRawTransaction = true;
bool fEnableKeyPoolRefill = true;
bool fEnableSetTxFee = true;
#endif

std::optional<std::string> LoadAllowedDeprecatedFeatures() {
    auto args = GetMultiArg("-allowdeprecated");
    std::set<std::string> allowdeprecated(args.begin(), args.end());

    if (allowdeprecated.count("none") > 0) {
        if (allowdeprecated.size() > 1)
            return "When using -allowdeprecated=none no other values may be provided for -allowdeprecated.";
        allowdeprecated = {};
    } else {
        allowdeprecated.insert(DEFAULT_ALLOW_DEPRECATED.begin(), DEFAULT_ALLOW_DEPRECATED.end());
    }

    std::set<std::string> unrecognized;
    for (const auto& flag : allowdeprecated) {
        if (DEFAULT_ALLOW_DEPRECATED.count(flag) == 0 && DEFAULT_DENY_DEPRECATED.count(flag) == 0)
            unrecognized.insert(flag);
    }

    if (unrecognized.size() > 0) {
        std::string unrecMsg;
        for (const auto& value : unrecognized) {
            if (unrecMsg.size() > 0) unrecMsg += ", ";
            unrecMsg += "\"" + value + "\"";
        }

        return strprintf(
                "Unrecognized argument(s) to -allowdeprecated: %s;\n"
                "Please select from the following values: %s",
                unrecMsg, GetAllowableDeprecatedFeatures());
    }

    fEnableGbtOldHashes = allowdeprecated.count("gbt_oldhashes") > 0;
    fEnableAddrTypeField = allowdeprecated.count("addrtype") > 0;
#ifdef ENABLE_WALLET
    fEnableLegacyPrivacyStrategy = allowdeprecated.count("legacy_privacy") > 0;
    fEnableGetNewAddress = allowdeprecated.count("getnewaddress") > 0;
    fEnableGetRawChangeAddress = allowdeprecated.count("getrawchangeaddress") > 0;
    fEnableZGetNewAddress = allowdeprecated.count("z_getnewaddress") > 0;
    fEnableZGetBalance = allowdeprecated.count("z_getbalance") > 0;
    fEnableZGetTotalBalance = allowdeprecated.count("z_gettotalbalance") > 0;
    fEnableZListAddresses = allowdeprecated.count("z_listaddresses") > 0;
    fEnableWalletTxVJoinSplit = allowdeprecated.count("wallettxvjoinsplit") > 0;
#endif

    return std::nullopt;
}

std::string GetAllowableDeprecatedFeatures() {
    std::string result = "\"none\"";
    for (const auto& value : DEFAULT_ALLOW_DEPRECATED) {
        result += ", \"" + value + "\"";
    }
    for (const auto& value : DEFAULT_DENY_DEPRECATED) {
        result += ", \"" + value + "\"";
    }
    return result;
}

std::string Deprecated(bool enabled, std::string method, std::string instead) {
    auto status = enabled ? "DEPRECATED" : "DISABLED";
    auto reenable = enabled
            ? std::string("")
            : (std::string("You can restart the node with `-allowdeprecated=") + method + "`\n"
               "to re-enable this method during its deprecation period.\n");

    return std::string("\n")
           + method + " is " + status + " and will be removed in a future release.\n"
           + instead + "\n"
           + reenable
           + "See https://zcash.github.io/zcash/user/deprecation.html for more information.\n";
}
