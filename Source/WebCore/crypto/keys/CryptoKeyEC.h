/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "CryptoKey.h"
#include "CryptoKeyPair.h"
#include "ExceptionOr.h"

#if ENABLE(SUBTLE_CRYPTO)

#if OS(DARWIN) && !PLATFORM(GTK)
typedef struct _CCECCryptor *CCECCryptorRef;
typedef CCECCryptorRef PlatformECKey;
#endif

#if PLATFORM(GTK)
// gcry_sexp* equates gcry_sexp_t.
struct gcry_sexp;
typedef gcry_sexp* PlatformECKey;
#endif


namespace WebCore {

struct JsonWebKey;

class EcKeyAlgorithm : public KeyAlgorithm {
public:
    EcKeyAlgorithm(const String& name, const String& curve)
        : KeyAlgorithm(name)
        , m_curve(curve)
    {
    }

    KeyAlgorithmClass keyAlgorithmClass() const override { return KeyAlgorithmClass::EC; }

    const String& namedCurve() const { return m_curve; }

private:
    String m_curve;
};

class CryptoKeyEC final : public CryptoKey {
public:
    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=169231
    enum class NamedCurve {
        P256,
        P384,
    };

    static Ref<CryptoKeyEC> create(CryptoAlgorithmIdentifier identifier, NamedCurve curve, CryptoKeyType type, PlatformECKey platformKey, bool extractable, CryptoKeyUsageBitmap usages)
    {
        return adoptRef(*new CryptoKeyEC(identifier, curve, type, platformKey, extractable, usages));
    }
    virtual ~CryptoKeyEC();

    static ExceptionOr<CryptoKeyPair> generatePair(CryptoAlgorithmIdentifier, const String& curve, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> importRaw(CryptoAlgorithmIdentifier, const String& curve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> importJwk(CryptoAlgorithmIdentifier, const String& curve, JsonWebKey&&, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> importSpki(CryptoAlgorithmIdentifier, const String& curve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> importPkcs8(CryptoAlgorithmIdentifier, const String& curve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap);

    ExceptionOr<Vector<uint8_t>> exportRaw() const;
    JsonWebKey exportJwk() const;
    ExceptionOr<Vector<uint8_t>> exportSpki() const;
    ExceptionOr<Vector<uint8_t>> exportPkcs8() const;

    size_t keySizeInBits() const;
    NamedCurve namedCurve() const { return m_curve; }
    String namedCurveString() const;
    PlatformECKey platformKey() { return m_platformKey; }
    static bool isValidECAlgorithm(CryptoAlgorithmIdentifier);

private:
    CryptoKeyEC(CryptoAlgorithmIdentifier, NamedCurve, CryptoKeyType, PlatformECKey, bool extractable, CryptoKeyUsageBitmap);

    CryptoKeyClass keyClass() const final { return CryptoKeyClass::EC; }

    std::unique_ptr<KeyAlgorithm> buildAlgorithm() const final;
    std::unique_ptr<CryptoKeyData> exportData() const final;

    static std::optional<CryptoKeyPair> platformGeneratePair(CryptoAlgorithmIdentifier, NamedCurve, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> platformImportRaw(CryptoAlgorithmIdentifier, NamedCurve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> platformImportJWKPublic(CryptoAlgorithmIdentifier, NamedCurve, Vector<uint8_t>&& x, Vector<uint8_t>&& y, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> platformImportJWKPrivate(CryptoAlgorithmIdentifier, NamedCurve, Vector<uint8_t>&& x, Vector<uint8_t>&& y, Vector<uint8_t>&& d, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> platformImportSpki(CryptoAlgorithmIdentifier, NamedCurve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap);
    static RefPtr<CryptoKeyEC> platformImportPkcs8(CryptoAlgorithmIdentifier, NamedCurve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap);
    Vector<uint8_t> platformExportRaw() const;
    void platformAddFieldElements(JsonWebKey&) const;
    Vector<uint8_t> platformExportSpki() const;
    Vector<uint8_t> platformExportPkcs8() const;

    PlatformECKey m_platformKey;
    NamedCurve m_curve;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CRYPTO_KEY(CryptoKeyEC, CryptoKeyClass::EC)

SPECIALIZE_TYPE_TRAITS_KEY_ALGORITHM(EcKeyAlgorithm, KeyAlgorithmClass::EC)

#endif // ENABLE(SUBTLE_CRYPTO)
