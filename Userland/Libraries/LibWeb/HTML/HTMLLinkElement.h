/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, the SerenityOS developers.
 * Copyright (c) 2021, Sam Atkins <atkinssj@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/DOM/DocumentLoadEventDelayer.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/HTML/HTMLElement.h>
#include <LibWeb/ReferrerPolicy/ReferrerPolicy.h>

namespace Web::HTML {

struct LinkProcessingOptions;

class HTMLLinkElement final
    : public HTMLElement
    , public ResourceClient {
    WEB_PLATFORM_OBJECT(HTMLLinkElement, HTMLElement);

public:
    virtual ~HTMLLinkElement() override;

    virtual void inserted() override;

    DeprecatedString rel() const { return attribute(HTML::AttributeNames::rel); }
    DeprecatedString type() const { return attribute(HTML::AttributeNames::type); }
    DeprecatedString href() const { return attribute(HTML::AttributeNames::href); }

    bool has_loaded_icon() const;
    bool load_favicon_and_use_if_window_is_active();

private:
    HTMLLinkElement(DOM::Document&, DOM::QualifiedName);

    LinkProcessingOptions create_link_options();
    JS::GCPtr<Fetch::Infrastructure::Request> create_link_request(LinkProcessingOptions const&);

    void fetch_and_process_the_linked_resource();
    void default_fetch_and_process_the_linked_resource();

    void process_linked_resource(bool success, JS::NonnullGCPtr<Fetch::Infrastructure::Response> response, Variant<Empty, Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag, ByteBuffer> body_bytes, AK::URL request_url);

    void process_linked_stylesheet_resource(bool success, JS::NonnullGCPtr<Fetch::Infrastructure::Response> response, Variant<Empty, Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag, ByteBuffer> body_bytes, AK::URL request_url);

    void parse_attribute(FlyString const&, DeprecatedString const&) override;

    // ^ResourceClient
    virtual void resource_did_fail() override;
    virtual void resource_did_load() override;

    // ^ HTMLElement
    virtual void did_remove_attribute(FlyString const&) override;
    virtual void visit_edges(Cell::Visitor&) override;

    void resource_did_load_favicon();

    struct Relationship {
        enum {
            Alternate = 1 << 0,
            Stylesheet = 1 << 1,
            Preload = 1 << 2,
            DNSPrefetch = 1 << 3,
            Preconnect = 1 << 4,
            Icon = 1 << 5,
        };
    };

    RefPtr<Resource> m_preload_resource;
    JS::GCPtr<CSS::CSSStyleSheet> m_associated_css_style_sheet;

    Optional<DOM::DocumentLoadEventDelayer> m_document_load_event_delayer;
    unsigned m_relationship { 0 };
};

}
