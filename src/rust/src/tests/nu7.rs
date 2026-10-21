use std::convert::TryFrom;

use proptest::prelude::*;
use zcash_primitives::transaction::{testing::arb_tx, Transaction};
use zcash_protocol::consensus::BranchId;

/// The ZIP 259 NU7 consensus branch id must be known to the Rust side, because every
/// v5/v6 transaction and every history node carries it once NU7 activates.
#[test]
fn nu7_branch_id_round_trips() {
    assert_eq!(BranchId::try_from(0x7719_0ad9u32), Ok(BranchId::Nu7));
    assert_eq!(u32::from(BranchId::Nu7), 0x7719_0ad9);
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(16))]

    /// Transactions stamped with the NU7 branch id parse and keep that branch id.
    #[test]
    fn nu7_transactions_parse(tx in arb_tx(BranchId::Nu7)) {
        let mut bytes = vec![];
        tx.write(&mut bytes).unwrap();
        let parsed = Transaction::read(&bytes[..], BranchId::Nu7).unwrap();
        prop_assert_eq!(parsed.consensus_branch_id(), BranchId::Nu7);
        prop_assert_eq!(parsed.txid(), tx.txid());
    }
}
