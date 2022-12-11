/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/PromiseCapability.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Fetch/BodyInit.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Bodies.h>
#include <LibWeb/Fetch/Infrastructure/Task.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::Fetch::Infrastructure {

Body::Body(JS::Handle<Streams::ReadableStream> stream)
    : m_stream(move(stream))
{
}

Body::Body(JS::Handle<Streams::ReadableStream> stream, SourceType source, Optional<u64> length)
    : m_stream(move(stream))
    , m_source(move(source))
    , m_length(move(length))
{
}

// https://fetch.spec.whatwg.org/#concept-body-clone
WebIDL::ExceptionOr<Body> Body::clone() const
{
    // To clone a body body, run these steps:

    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // FIXME: 1. Let « out1, out2 » be the result of teeing body’s stream.
    // FIXME: 2. Set body’s stream to out1.
    auto* out2 = vm.heap().allocate<Streams::ReadableStream>(realm, realm);

    // 3. Return a body whose stream is out2 and other members are copied from body.
    return Body { JS::make_handle(out2), m_source, m_length };
}

// https://fetch.spec.whatwg.org/#fully-reading-body-as-promise
JS::NonnullGCPtr<JS::PromiseCapability> Body::fully_read_as_promise() const
{
    auto& vm = Bindings::main_thread_vm();
    auto& realm = *vm.current_realm();

    // FIXME: Implement the streams spec - this is completely made up for now :^)
    if (auto const* byte_buffer = m_source.get_pointer<ByteBuffer>()) {
        auto result = DeprecatedString::copy(*byte_buffer);
        return WebIDL::create_resolved_promise(realm, JS::PrimitiveString::create(vm, move(result)));
    }
    // Empty, Blob, FormData
    return WebIDL::create_rejected_promise(realm, JS::InternalError::create(realm, "Reading body isn't fully implemented"sv));
}

// https://fetch.spec.whatwg.org/#body-fully-read
void Body::fully_read(JS::SafeFunction<void(Variant<ByteBuffer, Empty> const&)> process_body, JS::SafeFunction<void()> process_body_error, Variant<Empty, JS::NonnullGCPtr<JS::Object>> const& task_destination) const
{
    // FIXME: 1. If taskDestination is null, then set taskDestination to the result of starting a new parallel queue.
    VERIFY(task_destination.has<JS::NonnullGCPtr<JS::Object>>());
    auto& task_destination_as_object = *task_destination.get<JS::NonnullGCPtr<JS::Object>>();

    // 2. Let promise be the result of fully reading body as promise given body.
    auto promise = fully_read_as_promise();

    // 3. Let fulfilledSteps given a byte sequence bytes be to queue a fetch task to run processBody given bytes, with taskDestination.
    WebIDL::ReactionSteps fulfilled_steps = [&task_destination_as_object, process_body = move(process_body)](JS::Value value) mutable -> WebIDL::ExceptionOr<JS::Value> {
        queue_fetch_task(task_destination_as_object, [value, process_body = move(process_body)]() mutable {
            // FIXME: This is a hack assumption that Body::fully_read_as_promise() resolves with a string.
            //        This will obviously need to change once streams are implemented.
            process_body(MUST(ByteBuffer::copy(value.as_string().deprecated_string().bytes())));
        });
        return JS::js_undefined();
    };

    // 4. Let rejectedSteps be to queue a fetch task to run processBodyError, with taskDestination.
    WebIDL::ReactionSteps rejected_steps = [&task_destination_as_object, process_body_error = move(process_body_error)](JS::Value) mutable -> WebIDL::ExceptionOr<JS::Value> {
        queue_fetch_task(task_destination_as_object, [process_body_error = move(process_body_error)]() mutable {
            process_body_error();
        });
        return JS::js_undefined();
    };

    // 5. React to promise with fulfilledSteps and rejectedSteps.
    WebIDL::react_to_promise(promise, move(fulfilled_steps), move(rejected_steps));
}

// https://fetch.spec.whatwg.org/#byte-sequence-as-a-body
WebIDL::ExceptionOr<Body> byte_sequence_as_body(JS::Realm& realm, ReadonlyBytes bytes)
{
    // To get a byte sequence bytes as a body, return the body of the result of safely extracting bytes.
    auto [body, _] = TRY(safely_extract_body(realm, bytes));
    return body;
}

}
