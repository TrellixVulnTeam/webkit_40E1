/*
 * Copyright (C) 2014 Igalia S.L. All rights reserved.
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

#include "config.h"
#include "CryptoAlgorithmAES_CBC.h"

#if ENABLE(SUBTLE_CRYPTO)

#include "CryptoAlgorithmAesCbcCfbParams.h"
#include "CryptoKeyAES.h"
#include "ExceptionCode.h"
#include "NotImplemented.h"
#include "ScriptExecutionContext.h"
#include <pal/crypto/gcrypt/Handle.h>
#include <pal/crypto/gcrypt/Utilities.h>

namespace WebCore {

static std::optional<Vector<uint8_t>> gcryptEncrypt(const Vector<uint8_t>& key, const Vector<uint8_t>& iv, Vector<uint8_t>&& plainText)
{
    auto algorithm = PAL::GCrypt::aesAlgorithmForKeySize(key.size() * 8);
    if (!algorithm)
        return std::nullopt;

    PAL::GCrypt::Handle<gcry_cipher_hd_t> handle;
    gcry_error_t error = gcry_cipher_open(&handle, *algorithm, GCRY_CIPHER_MODE_CBC, 0);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setkey(handle, key.data(), key.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setiv(handle, iv.data(), iv.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    {
        // PKCS#7
        size_t size = plainText.size();

        // Round up the size value to the next multiple of the cipher's block length to get the padded size.
        size_t paddedSize = roundUpToMultipleOf(gcry_cipher_get_algo_blklen(*algorithm), size + 1);

        // Padded size should be bigger than size, but bail if the value doesn't fit into a byte.
        ASSERT(paddedSize > size);
        if (paddedSize - size > 255)
            return std::nullopt;
        uint8_t paddingValue = paddedSize - size;

        plainText.resize(paddedSize);
        std::memset(plainText.data() + size, paddingValue, paddingValue);
    }

    error = gcry_cipher_final(handle);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    Vector<uint8_t> output(plainText.size());
    error = gcry_cipher_encrypt(handle, output.data(), output.size(), plainText.data(), plainText.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    return output;
}

static std::optional<Vector<uint8_t>> gcryptDecrypt(const Vector<uint8_t>& key, const Vector<uint8_t>& iv, const Vector<uint8_t>& cipherText)
{
    auto algorithm = PAL::GCrypt::aesAlgorithmForKeySize(key.size() * 8);
    if (!algorithm)
        return std::nullopt;

    PAL::GCrypt::Handle<gcry_cipher_hd_t> handle;
    gcry_error_t error = gcry_cipher_open(&handle, *algorithm, GCRY_CIPHER_MODE_CBC, 0);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setkey(handle, key.data(), key.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_setiv(handle, iv.data(), iv.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    error = gcry_cipher_final(handle);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    Vector<uint8_t> output(cipherText.size());
    error = gcry_cipher_decrypt(handle, output.data(), output.size(), cipherText.data(), cipherText.size());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    {
        // PKCS#7
        uint8_t paddingValue = output.last();
        if (paddingValue > gcry_cipher_get_algo_blklen(*algorithm))
            return std::nullopt;

        size_t size = output.size();
        if (paddingValue > size)
            return std::nullopt;

        if (std::count(output.end() - paddingValue, output.end(), paddingValue) != paddingValue)
            return std::nullopt;

        output.resize(size - paddingValue);
    }

    return output;
}

void CryptoAlgorithmAES_CBC::platformEncrypt(std::unique_ptr<CryptoAlgorithmParameters>&& parameters, Ref<CryptoKey>&& key, Vector<uint8_t>&& plainText, VectorCallback&& callback, ExceptionCallback&& exceptionCallback, ScriptExecutionContext& context, WorkQueue& workQueue)
{
    context.ref();
    workQueue.dispatch(
        [parameters = WTFMove(parameters), key = WTFMove(key), plainText = WTFMove(plainText), callback = WTFMove(callback), exceptionCallback = WTFMove(exceptionCallback), &context]() mutable {
            auto& aesParameters = downcast<CryptoAlgorithmAesCbcCfbParams>(*parameters);
            auto& aesKey = downcast<CryptoKeyAES>(key.get());

            auto output = gcryptEncrypt(aesKey.key(), aesParameters.ivVector(), WTFMove(plainText));
            if (!output) {
                // We should only dereference callbacks after being back to the Document/Worker threads.
                context.postTask(
                    [callback = WTFMove(callback), exceptionCallback = WTFMove(exceptionCallback)](ScriptExecutionContext& context) {
                        exceptionCallback(OperationError);
                        context.deref();
                    });
                return;
            }

            // We should only dereference callbacks after being back to the Document/Worker threads.
            context.postTask(
                [output = WTFMove(*output), callback = WTFMove(callback), exceptionCallback = WTFMove(exceptionCallback)](ScriptExecutionContext& context) mutable {
                    callback(WTFMove(output));
                    context.deref();
                });
        });
}

void CryptoAlgorithmAES_CBC::platformDecrypt(std::unique_ptr<CryptoAlgorithmParameters>&& parameters, Ref<CryptoKey>&& key, Vector<uint8_t>&& cipherText, VectorCallback&& callback, ExceptionCallback&& exceptionCallback, ScriptExecutionContext& context, WorkQueue& workQueue)
{
    context.ref();
    workQueue.dispatch(
        [parameters = WTFMove(parameters), key = WTFMove(key), cipherText = WTFMove(cipherText), callback = WTFMove(callback), exceptionCallback = WTFMove(exceptionCallback), &context]() mutable {
            auto& aesParameters = downcast<CryptoAlgorithmAesCbcCfbParams>(*parameters);
            auto& aesKey = downcast<CryptoKeyAES>(key.get());

            auto output = gcryptDecrypt(aesKey.key(), aesParameters.ivVector(), WTFMove(cipherText));
            if (!output) {
                // We should only dereference callbacks after being back to the Document/Worker threads.
                context.postTask(
                    [callback = WTFMove(callback), exceptionCallback = WTFMove(exceptionCallback)](ScriptExecutionContext& context) {
                        exceptionCallback(OperationError);
                        context.deref();
                    });
                return;
            }

            // We should only dereference callbacks after being back to the Document/Worker threads.
            context.postTask(
                [output = WTFMove(*output), callback = WTFMove(callback), exceptionCallback = WTFMove(exceptionCallback)](ScriptExecutionContext& context) mutable {
                    callback(WTFMove(output));
                    context.deref();
                });
        });
}

ExceptionOr<void> CryptoAlgorithmAES_CBC::platformEncrypt(const CryptoAlgorithmAesCbcParamsDeprecated&, const CryptoKeyAES&, const CryptoOperationData&, VectorCallback&&, VoidCallback&&)
{
    notImplemented();
    return Exception { NOT_SUPPORTED_ERR };
}

ExceptionOr<void> CryptoAlgorithmAES_CBC::platformDecrypt(const CryptoAlgorithmAesCbcParamsDeprecated&, const CryptoKeyAES&, const CryptoOperationData&, VectorCallback&&, VoidCallback&&)
{
    notImplemented();
    return Exception { NOT_SUPPORTED_ERR };
}

} // namespace WebCore

#endif // ENABLE(SUBTLE_CRYPTO)
