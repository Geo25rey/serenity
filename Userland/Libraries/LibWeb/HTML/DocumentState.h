/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/URL.h>
#include <LibJS/Heap/Cell.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/Origin.h>
#include <LibWeb/HTML/PolicyContainers.h>
#include <LibWeb/ReferrerPolicy/ReferrerPolicy.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-2
class DocumentState final : public JS::Cell {
    JS_CELL(DocumentState, JS::Cell);

public:
    virtual ~DocumentState();

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-document
    JS::GCPtr<DOM::Document> document;

    enum class Client {
        Tag,
    };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-history-policy-container
    Variant<PolicyContainer, Client> history_policy_container { Client::Tag };

    enum class NoReferrer {
        Tag,
    };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-request-referrer
    Variant<NoReferrer, Client, AK::URL> request_referrer { Client::Tag };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-request-referrer-policy
    ReferrerPolicy::ReferrerPolicy request_referrer_policy { ReferrerPolicy::DEFAULT_REFERRER_POLICY };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-initiator-origin
    Optional<HTML::Origin> initiator_origin;

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-origin
    Optional<HTML::Origin> origin;

    // FIXME: https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-nested-histories

    // FIXME: https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-resource

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-reload-pending
    bool reload_pending { false };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-ever-populated
    bool ever_populated { false };

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#document-state-nav-target-name
    DeprecatedString navigable_target_name { "" };

private:
    DocumentState();

    void visit_edges(Cell::Visitor&) override;
};

}
