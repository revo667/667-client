#ifndef ENGINE_SHARED_ED25519_H
#define ENGINE_SHARED_ED25519_H

#include <cstddef>

static constexpr size_t ED25519_PUBLIC_KEY_LENGTH = 32;
static constexpr size_t ED25519_SIGNATURE_LENGTH = 64;

extern "C" bool ed25519_verify(const unsigned char *pPublicKey, const unsigned char *pMessage, size_t MessageLength, const unsigned char *pSignature);

#endif
