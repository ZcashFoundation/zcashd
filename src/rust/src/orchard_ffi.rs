use std::convert::TryInto;

use rand_core::OsRng;
use tracing::{debug, error};

use crate::{
    bundlecache::{orchard_bundle_validity_cache, orchard_bundle_validity_cache_mut, CacheEntries},
    orchard_bundle::Bundle,
};

struct BatchValidatorInner {
    // The lifetime here applies to a reference to a static VerifyingKey
    validator: orchard::bundle::BatchValidator<'static>,
    queued_entries: CacheEntries,
    valid: bool,
}

pub(crate) struct BatchValidator(Option<BatchValidatorInner>);

/// Creates an Orchard bundle batch validation context for the circuit in force at the height
/// of the block being validated: the NU6.2 fixed circuit if `nu6_2_active`, else the pre-NU6.2
/// circuit.
pub(crate) fn orchard_batch_validation_init(
    cache_store: bool,
    circuit_version: crate::bridge::ffi::OrchardCircuitVersion,
) -> Box<BatchValidator> {
    // NOTE(azmr): we convert back to the real type ASAP to try to catch future enum variants
    let circuit_version = match circuit_version {
        crate::bridge::ffi::OrchardCircuitVersion::PostNu6_3 => {
            orchard::circuit::OrchardCircuitVersion::PostNu6_3
        }
        crate::bridge::ffi::OrchardCircuitVersion::FixedPostNu6_2 => {
            orchard::circuit::OrchardCircuitVersion::FixedPostNu6_2
        }
        crate::bridge::ffi::OrchardCircuitVersion::InsecurePreNu6_2 => {
            orchard::circuit::OrchardCircuitVersion::InsecurePreNu6_2
        }
        _ => panic!(
            "invalid orchard circuit version enum value: {}",
            circuit_version.repr
        ),
    };
    let vk: &'static orchard::circuit::VerifyingKey = match circuit_version {
        orchard::circuit::OrchardCircuitVersion::PostNu6_3 => &crate::ORCHARD_VK_NU6_3,
        orchard::circuit::OrchardCircuitVersion::FixedPostNu6_2 => &crate::ORCHARD_VK_FIXED,
        orchard::circuit::OrchardCircuitVersion::InsecurePreNu6_2 => &crate::ORCHARD_VK_INSECURE,
    };
    Box::new(BatchValidator(Some(BatchValidatorInner {
        validator: orchard::bundle::BatchValidator::new(vk),
        queued_entries: CacheEntries::new(cache_store),
        valid: true,
    })))
}

impl BatchValidatorInner {
    fn poison(&mut self, error_msg: &str) {
        error!("{}", error_msg);
        self.valid = false;
    }
}

impl BatchValidator {
    /// Adds an Orchard bundle to this batch.
    pub(crate) fn add_bundle(&mut self, bundle: Box<Bundle>, sighash: [u8; 32]) {
        let batch = self.0.as_mut();
        let bundle = bundle.inner();

        match (batch, bundle) {
            (Some(batch), Some(bundle)) => {
                let cache = orchard_bundle_validity_cache();

                let tx_version = match bundle.bundle_version().note_version() {
                    orchard::NoteVersion::V2 => orchard::bundle::TxVersion::V5,
                    orchard::NoteVersion::V3 => orchard::bundle::TxVersion::V6,
                };

                // Compute the cache entry for this bundle.
                let cache_entry = {
                    let bundle_commitment = match bundle.commitment(tx_version) {
                        Ok(bundle_commitment) => bundle_commitment,
                        Err(e) => {
                            batch.poison(&format!(
                                "bundle commitment must be computable for its transaction version: {}",
                                e
                            ));
                            return;
                        }
                    };
                    let bundle_authorizing_commitment = match bundle
                        .authorizing_commitment(tx_version)
                    {
                        Ok(bundle_authorizing_commitment) => bundle_authorizing_commitment,
                        Err(e) => {
                            batch.poison(&format!(
                                "bundle authorizing commitment must be computable for its transaction version: {}",
                                e
                            ));
                            return;
                        }
                    };
                    let Ok(bundle_commitment) = bundle_commitment.0.as_bytes().try_into() else {
                        batch.poison("bundle commitment has invalid length");
                        return;
                    };
                    let Ok(bundle_authorizing_commitment) =
                        bundle_authorizing_commitment.0.as_bytes().try_into()
                    else {
                        batch.poison("bundle authorizing commitment has invalid length");
                        return;
                    };
                    cache.compute_entry(bundle_commitment, bundle_authorizing_commitment, &sighash)
                };

                // Check if this bundle's validation result exists in the cache.
                if !cache.contains(cache_entry, &mut batch.queued_entries) {
                    // The bundle has been added to `inner.queued_entries` because it was not
                    // in the cache. We now add its authorization to the validation batch.
                    if batch.validator.add_bundle(bundle, sighash).is_err() {
                        batch.poison("invalid bundle");
                    }
                }
            }
            (Some(_), None) => debug!("Tx has no Orchard component"),
            (None, _) => error!("orchard::BatchValidator has already been used"),
        }
    }

    /// Validates this batch.
    ///
    /// - Returns `true` if `batch` is null.
    /// - Returns `false` if any item in the batch is invalid.
    ///
    /// The batch validation context is freed by this function.
    ///
    /// ## Consensus rules
    ///
    /// [§4.6](https://zips.z.cash/protocol/protocol.pdf#actiondesc):
    /// - Canonical element encodings are enforced by [`orchard_bundle_parse`].
    /// - SpendAuthSig^Orchard validity is enforced here.
    /// - Proof validity is enforced here.
    ///
    /// [§7.1](https://zips.z.cash/protocol/protocol.pdf#txnencodingandconsensus):
    /// - `bindingSigOrchard` validity is enforced here.
    pub(crate) fn validate(&mut self) -> bool {
        if let Some(inner) = self.0.take() {
            if !inner.valid {
                return false;
            }

            // The verifying key for this batch's circuit was fixed at construction
            // (`orchard_batch_validation_init`).
            if inner.validator.validate(OsRng) {
                // `BatchValidator::validate()` is only called if every
                // `BatchValidator::check_bundle()` returned `true`, so at this point
                // every bundle that was added to `inner.queued_entries` has valid
                // authorization.
                orchard_bundle_validity_cache_mut().insert(inner.queued_entries);
                true
            } else {
                false
            }
        } else {
            error!("orchard::BatchValidator has already been used");
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use crate::orchard_bundle::none_orchard_bundle;

    fn batch_validator(valid: bool) -> BatchValidator {
        BatchValidator(Some(BatchValidatorInner {
            validator: orchard::bundle::BatchValidator::new(&crate::ORCHARD_VK_INSECURE),
            queued_entries: CacheEntries::new(false),
            valid,
        }))
    }

    #[test]
    fn absent_bundle_does_not_poison_batch() {
        let mut batch = batch_validator(true);

        batch.add_bundle(none_orchard_bundle(), [0; 32]);

        assert!(batch.0.as_ref().unwrap().valid);
    }

    #[test]
    fn poisoned_batch_validation_fails_without_using_cache() {
        let mut batch = batch_validator(true);
        batch.0.as_mut().unwrap().poison("test poison");

        // The bundle cache is intentionally not initialized in this test. If a
        // poisoned batch tried to populate the cache, validation would panic.
        assert!(!batch.validate());
        assert!(batch.0.is_none());
    }
}
