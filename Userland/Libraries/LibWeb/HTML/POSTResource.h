/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/DeprecatedString.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#post-resource
struct POSTResource {
public:
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#post-resource-request-body
    // A request body, a byte sequence or failure.
    Optional<ByteBuffer> request_body;

    enum class RequestContentType {
        ApplicationXWWWFormUrlencoded,
        MultipartFormData,
        TextPlain,
    };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#post-resource-request-content-type
    // A request content-type, which is `application/x-www-form-urlencoded`, `multipart/form-data`, or `text/plain`.
    RequestContentType request_content_type {};
};

}
