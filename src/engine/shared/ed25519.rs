use ed25519_dalek::Signature;
use ed25519_dalek::VerifyingKey;
use std::slice;

#[allow(missing_docs, clippy::missing_safety_doc)]
#[no_mangle]
pub unsafe extern "C" fn ed25519_verify(
    public_key: *const u8,
    message: *const u8,
    message_len: usize,
    signature: *const u8,
) -> bool {
    if public_key.is_null() || signature.is_null() || (message.is_null() && message_len != 0) {
        return false;
    }
    let public_key: &[u8; 32] = unsafe { &*(public_key as *const [u8; 32]) };
    let signature: &[u8; 64] = unsafe { &*(signature as *const [u8; 64]) };
    let message = if message_len == 0 {
        &[][..]
    } else {
        unsafe { slice::from_raw_parts(message, message_len) }
    };
    let Ok(key) = VerifyingKey::from_bytes(public_key) else {
        return false;
    };
    key.verify_strict(message, &Signature::from_bytes(signature))
        .is_ok()
}
