/*
 * Copyright (C) 2016 Canon Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted, provided that the following conditions
 * are required to be met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Canon Inc. nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CANON INC. AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CANON INC. AND ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "JSReadableStreamSource.h"

#if ENABLE(STREAMS_API)

using namespace JSC;

namespace WebCore {

static void startReadableStream(JSC::ExecState& state, Ref<DeferredPromise>&& promise)
{
    VM& vm = state.vm();
    JSReadableStreamSource* source = jsDynamicDowncast<JSReadableStreamSource*>(vm, state.thisValue());
    ASSERT(source);

    ASSERT(state.argumentCount());
    JSReadableStreamDefaultController* controller = jsDynamicDowncast<JSReadableStreamDefaultController*>(vm, state.uncheckedArgument(0));
    ASSERT(controller);

    source->wrapped().start(ReadableStreamDefaultController(controller), WTFMove(promise));
}

JSValue JSReadableStreamSource::start(ExecState& state)
{
    VM& vm = state.vm();
    ASSERT(state.argumentCount());
    JSReadableStreamDefaultController* controller = jsDynamicDowncast<JSReadableStreamDefaultController*>(vm, state.uncheckedArgument(0));
    ASSERT(controller);

    m_controller.set(vm, this, controller);

    return callPromiseFunction<startReadableStream, PromiseExecutionScope::WindowOrWorker>(state);
}

static void pullReadableStream(JSC::ExecState& state, Ref<DeferredPromise>&& promise)
{
    VM& vm = state.vm();
    JSReadableStreamSource* source = jsDynamicDowncast<JSReadableStreamSource*>(vm, state.thisValue());
    ASSERT(source);

    source->wrapped().pull(WTFMove(promise));
}

JSValue JSReadableStreamSource::pull(ExecState& state)
{
    return callPromiseFunction<pullReadableStream, PromiseExecutionScope::WindowOrWorker>(state);
}

JSValue JSReadableStreamSource::controller(ExecState&) const
{
    ASSERT_NOT_REACHED();
    return jsUndefined();
}

}

#endif // ENABLE(STREAMS_API)
