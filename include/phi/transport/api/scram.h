#pragma once

// SCRAM-SHA-256 (RFC 5802, RFC 7677) without channel binding: the login of the
// phi protocol, both halves of it.
//
// Core checks proofs and keeps verifiers; a client (the UI in TypeScript,
// phi-cli here) derives the verifier when it sets a password, so the password
// never reaches the box. The derivation lived twice in C++ - core's scram.cpp
// and phi-cli's own copy with its own iteration constant - and two copies of a
// key derivation are one too many. This header is the one place it comes from.
//
// The point of the scheme is one property: the password never leaves the
// client. What crosses the wire is a proof over a transcript both sides built
// independently, and a fresh nonce makes that proof worthless once used. The
// server proves itself too: the final answer carries a signature only
// something holding ServerKey can produce. What it does not buy: a recorded
// login is enough for an *offline* guess at the password; only a confidential
// channel (HTTPS) stops that, and the iteration count is what makes guessing
// expensive meanwhile.
//
// Pure functions over bytes, so the scheme is tested against the RFC 7677
// vector rather than against itself. Header-only on OpenSSL's libcrypto: a
// target that includes it links OpenSSL::Crypto.

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <phi/runtime/str.h>

#include <cstdint>
#include <string>

namespace phicore::scram {

/// Length of every key and signature below. SHA-256, so 32.
inline constexpr int kKeyBytes = 32;

/// PBKDF2 rounds for a new verifier. The client pays them (about a third of a
/// second in the browser), so the number follows what an attacker's hardware
/// can do rather than what an event loop can sit through.
inline constexpr int kDefaultIterations = 600000;

/// Bounds for a verifier a client asks core to store: below the floor it is
/// cheap to attack, above the ceiling a client could lock every future login
/// behind a derivation nobody wants to wait for.
inline constexpr int kMinIterations = 100000;
inline constexpr int kMaxIterations = 4000000;

namespace detail {

[[nodiscard]] inline std::string hmac(const std::string &key, const std::string &message)
{
    std::string out(SHA256_DIGEST_LENGTH, '\0');
    unsigned int written = 0;
    if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
              reinterpret_cast<const unsigned char *>(message.data()), message.size(),
              reinterpret_cast<unsigned char *>(out.data()), &written)
        || written != SHA256_DIGEST_LENGTH) {
        return {};
    }
    return out;
}

} // namespace detail

/// SaltedPassword := PBKDF2-HMAC-SHA-256(password, salt, iterations); empty
/// when OpenSSL fails or the rounds are not positive.
[[nodiscard]] inline std::string saltedPassword(const std::string &password,
                                               const std::string &salt,
                                               int iterations)
{
    if (iterations <= 0)
        return {};
    std::string out(kKeyBytes, '\0');
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char *>(salt.data()),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(), kKeyBytes,
                          reinterpret_cast<unsigned char *>(out.data()))
        != 1) {
        return {};
    }
    return out;
}

/// ClientKey := HMAC(SaltedPassword, "Client Key")
[[nodiscard]] inline std::string clientKey(const std::string &saltedPassword)
{
    return detail::hmac(saltedPassword, "Client Key");
}

/// StoredKey := SHA-256(ClientKey). What the server keeps: it can check a
/// proof with it but cannot produce one from it.
[[nodiscard]] inline std::string storedKey(const std::string &clientKey)
{
    std::string out(SHA256_DIGEST_LENGTH, '\0');
    unsigned int written = 0;
    if (EVP_Digest(clientKey.data(), clientKey.size(),
                   reinterpret_cast<unsigned char *>(out.data()), &written, EVP_sha256(), nullptr)
            != 1
        || written != SHA256_DIGEST_LENGTH) {
        return {};
    }
    return out;
}

/// ServerKey := HMAC(SaltedPassword, "Server Key")
[[nodiscard]] inline std::string serverKey(const std::string &saltedPassword)
{
    return detail::hmac(saltedPassword, "Server Key");
}

/// ClientSignature := HMAC(StoredKey, authMessage)
[[nodiscard]] inline std::string clientSignature(const std::string &storedKey,
                                                const std::string &authMessage)
{
    return detail::hmac(storedKey, authMessage);
}

/// ServerSignature := HMAC(ServerKey, authMessage)
[[nodiscard]] inline std::string serverSignature(const std::string &serverKey,
                                                const std::string &authMessage)
{
    return detail::hmac(serverKey, authMessage);
}

/// ClientProof := ClientKey XOR ClientSignature. Empty when the two differ in
/// length, which no caller should manage to do.
[[nodiscard]] inline std::string xorBytes(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size())
        return {};
    std::string out(lhs.size(), '\0');
    for (std::size_t i = 0; i < lhs.size(); ++i)
        out[i] = static_cast<char>(lhs[i] ^ rhs[i]);
    return out;
}

/// The `n=` field's escaping: a username may contain a comma or an equals
/// sign, and either would end a field early and let two different logins
/// produce the same transcript. RFC 5802 escapes them as =2C and =3D.
[[nodiscard]] inline std::string escapeUsername(const std::string &username)
{
    std::string out;
    out.reserve(username.size());
    for (const char c : username) {
        if (c == ',')
            out += "=2C";
        else if (c == '=')
            out += "=3D";
        else
            out += c;
    }
    return out;
}

/// The transcript both sides sign, built exactly as RFC 5802 builds it from
/// the fields that crossed as JSON:
///
///     n=<user>,r=<cnonce>,r=<cnonce><snonce>,s=<salt>,i=<rounds>,c=biws,r=<cnonce><snonce>
///
/// `biws` is base64 of "n,," - no channel binding, said the RFC's way.
[[nodiscard]] inline std::string authMessage(const std::string &username,
                                            const std::string &clientNonce,
                                            const std::string &serverNonce,
                                            const std::string &salt,
                                            int iterations)
{
    const std::string combined = clientNonce + serverNonce;
    const std::string clientFirstBare = "n=" + escapeUsername(username) + ",r=" + clientNonce;
    const std::string serverFirst =
        "r=" + combined + ",s=" + phi::str::toBase64(salt) + ",i=" + phi::str::number(iterations);
    const std::string clientFinalNoProof = "c=biws,r=" + combined;
    return clientFirstBare + ',' + serverFirst + ',' + clientFinalNoProof;
}

/// Comparison whose duration does not depend on where two values differ.
[[nodiscard]] inline bool constantTimeEquals(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i)
        diff |= static_cast<std::uint8_t>(lhs[i]) ^ static_cast<std::uint8_t>(rhs[i]);
    return diff == 0;
}

/// True when `nonce` is one of core's to look at: lowercase hex, 16 bytes or
/// more (the shortest core produces), and not so long that a caller could make
/// core carry a megabyte around as a lookup key.
[[nodiscard]] inline bool isPlausibleNonce(const std::string &nonce)
{
    if (nonce.size() < 32 || nonce.size() > 128)
        return false;
    for (const char c : nonce) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

} // namespace phicore::scram
