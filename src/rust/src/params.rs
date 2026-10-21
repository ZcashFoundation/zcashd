use memuse::DynamicUsage;
use zcash_protocol::{
    consensus::{self, BlockHeight, BranchId, NetworkType},
    local_consensus::LocalNetwork,
};

/// Chain parameters for the networks supported by `zcashd`.
///
/// Activation heights come from the C++ chainparams on every network, so setting a
/// height in `chainparams.cpp` never needs a matching crate release.
#[derive(Clone, Copy)]
pub(crate) struct Network {
    network_type: NetworkType,
    heights: LocalNetwork,
}

impl DynamicUsage for Network {
    fn dynamic_usage(&self) -> usize {
        // We know that `Option<BlockHeight>` allocates no memory.
        0
    }

    fn dynamic_usage_bounds(&self) -> (usize, Option<usize>) {
        (0, Some(0))
    }
}

/// Constructs a `Network` from the given network string and the C++ activation
/// heights, where a negative height means the upgrade is not scheduled.
#[allow(clippy::too_many_arguments)]
pub(crate) fn network(
    network: &str,
    overwinter: i32,
    sapling: i32,
    blossom: i32,
    heartwood: i32,
    canopy: i32,
    nu5: i32,
    nu6: i32,
    nu6_1: i32,
    nu6_2: i32,
    nu6_3: i32,
    nu7: i32,
) -> Result<Box<Network>, &'static str> {
    let network_type = match network {
        "main" => NetworkType::Main,
        "test" => NetworkType::Test,
        "regtest" => NetworkType::Regtest,
        _ => return Err("Unsupported network kind"),
    };

    let i32_to_optional_height = |n: i32| {
        if n.is_negative() {
            None
        } else {
            Some(BlockHeight::from_u32(n.unsigned_abs()))
        }
    };

    Ok(Box::new(Network {
        network_type,
        heights: LocalNetwork {
            overwinter: i32_to_optional_height(overwinter),
            sapling: i32_to_optional_height(sapling),
            blossom: i32_to_optional_height(blossom),
            heartwood: i32_to_optional_height(heartwood),
            canopy: i32_to_optional_height(canopy),
            nu5: i32_to_optional_height(nu5),
            nu6: i32_to_optional_height(nu6),
            nu6_1: i32_to_optional_height(nu6_1),
            nu6_2: i32_to_optional_height(nu6_2),
            nu6_3: i32_to_optional_height(nu6_3),
            nu7: i32_to_optional_height(nu7),
        },
    }))
}

/// Returns the consensus branch id that `network` assigns to the block at `height`.
///
/// zcashd checks this against its own C++ upgrade table at startup, so the two sides
/// cannot silently disagree about which epoch a height is in.
pub(crate) fn branch_id(network: &Network, height: u32) -> u32 {
    BranchId::for_height(network, BlockHeight::from_u32(height)).into()
}

impl consensus::Parameters for Network {
    fn network_type(&self) -> NetworkType {
        self.network_type
    }

    fn activation_height(&self, nu: consensus::NetworkUpgrade) -> Option<BlockHeight> {
        self.heights.activation_height(nu)
    }
}

#[cfg(test)]
mod tests {
    use zcash_protocol::consensus::{
        BranchId, NetworkType, NetworkUpgrade, Parameters, MAIN_NETWORK, TEST_NETWORK,
    };

    use super::{branch_id, network};

    /// Public heights through NU6.3 match the crate's tables. Mainnet NU7 uses the
    /// height supplied by C++, independently of the crate's older built-in schedule.
    #[test]
    fn public_network_activation_schedules() {
        let main = network(
            "main", 347_500, 419_200, 653_600, 903_000, 1_046_400, 1_687_104, 2_726_400, 3_146_400,
            3_364_600, 3_428_143, 3_543_405,
        )
        .unwrap();
        let test = network(
            "test", 207_500, 280_000, 584_000, 903_800, 1_028_500, 1_842_420, 2_976_000, 3_536_500,
            4_052_000, 4_134_000, 4_465_026,
        )
        .unwrap();

        assert_eq!(main.network_type(), NetworkType::Main);
        assert_eq!(test.network_type(), NetworkType::Test);
        for nu in [
            NetworkUpgrade::Overwinter,
            NetworkUpgrade::Sapling,
            NetworkUpgrade::Blossom,
            NetworkUpgrade::Heartwood,
            NetworkUpgrade::Canopy,
            NetworkUpgrade::Nu5,
            NetworkUpgrade::Nu6,
            NetworkUpgrade::Nu6_1,
            NetworkUpgrade::Nu6_2,
            NetworkUpgrade::Nu6_3,
        ] {
            assert_eq!(
                main.activation_height(nu),
                MAIN_NETWORK.activation_height(nu)
            );
            assert_eq!(
                test.activation_height(nu),
                TEST_NETWORK.activation_height(nu)
            );
        }
        assert_eq!(
            test.activation_height(NetworkUpgrade::Nu7),
            TEST_NETWORK.activation_height(NetworkUpgrade::Nu7)
        );
        const MAINNET_NU7: u32 = 3_543_405;
        assert_eq!(
            main.activation_height(NetworkUpgrade::Nu7),
            Some(MAINNET_NU7.into())
        );
        for (height, expected) in [
            (MAINNET_NU7 - 1, BranchId::Nu6_3),
            (MAINNET_NU7, BranchId::Nu7),
            (MAINNET_NU7 + 1, BranchId::Nu7),
        ] {
            assert_eq!(BranchId::for_height(main.as_ref(), height.into()), expected);
            assert_eq!(branch_id(&main, height), u32::from(expected));
        }
    }

    /// The NU7 height passed from C++ selects the NU7 branch id from that height on.
    #[test]
    fn nu7_height_is_plumbed() {
        let params = network("regtest", 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 150).unwrap();

        assert_eq!(params.network_type(), NetworkType::Regtest);
        assert_eq!(
            BranchId::for_height(params.as_ref(), 149.into()),
            BranchId::Nu6_3
        );
        assert_eq!(
            BranchId::for_height(params.as_ref(), 150.into()),
            BranchId::Nu7
        );
        assert_eq!(branch_id(&params, 150), 0x7719_0ad9);
    }
}
