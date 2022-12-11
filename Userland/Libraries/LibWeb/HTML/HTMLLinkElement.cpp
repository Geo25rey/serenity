/*
 * Copyright (c) 2018-2022, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, the SerenityOS developers.
 * Copyright (c) 2021, Sam Atkins <atkinssj@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Debug.h>
#include <AK/URL.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/Fetch/Fetching/Fetching.h>
#include <LibWeb/Fetch/Infrastructure/FetchAlgorithms.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Responses.h>
#include <LibWeb/HTML/CrossOrigin/CORSSettingsAttribute.h>
#include <LibWeb/HTML/HTMLLinkElement.h>
#include <LibWeb/HTML/HTMLStyleElement.h>
#include <LibWeb/Infra/CharacterTypes.h>
#include <LibWeb/Loader/ResourceLoader.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Platform/ImageCodecPlugin.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/semantics.html#link-processing-options
struct LinkProcessingOptions {
    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-href
    DeprecatedString href = "";

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-destination
    Optional<Fetch::Infrastructure::Request::Destination> destination = {};

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-initiator
    DeprecatedString initiatior = "link";

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-integrity
    DeprecatedString integrity = "";

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-type
    DeprecatedString type = "";

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-nonce
    DeprecatedString cryptographic_nonce_metadata = "";

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-crossorigin
    CORSSettingsAttribute crossorigin = CORSSettingsAttribute::NoCORS;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-referrer-policy
    Optional<ReferrerPolicy::ReferrerPolicy> referrer_policy;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-source-set
    // FIXME: Figure out the right type for this thing.
    void* source_set = nullptr;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-base-url
    AK::URL base_url;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-origin
    Origin origin;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-environment
    Environment& environment;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-policy-container
    PolicyContainer policy_container;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-document
    JS::GCPtr<DOM::Document> document;

    // https://html.spec.whatwg.org/multipage/semantics.html#link-options-on-document-ready
    Function<void(DOM::Document&)> on_document_ready;
};

HTMLLinkElement::HTMLLinkElement(DOM::Document& document, DOM::QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
    set_prototype(&Bindings::cached_web_prototype(realm(), "HTMLLinkElement"));
}

HTMLLinkElement::~HTMLLinkElement() = default;

// https://html.spec.whatwg.org/multipage/semantics.html#fetch-and-process-the-linked-resource
void HTMLLinkElement::fetch_and_process_the_linked_resource()
{
    // FIXME: This should be overridable by some resource types.
    default_fetch_and_process_the_linked_resource();
}

// https://html.spec.whatwg.org/multipage/semantics.html#create-link-options-from-element
LinkProcessingOptions HTMLLinkElement::create_link_options()
{
    // 1. Let document be el's node document.
    auto& document = this->document();

    // 2. Let options be a new link processing options with
    auto options = LinkProcessingOptions {
        // FIXME: destination: the result of translating the state of el's as attribute

        // FIXME: crossorigin: the state of el's crossorigin content attribute

        // FIXME: referrer policy: the state of el's referrerpolicy content attribute
        .referrer_policy = ReferrerPolicy::ReferrerPolicy {},

        // FIXME: source set: el's source set
        .source_set = nullptr,

        // base URL: document's URL
        .base_url = document.url(),

        // origin: document's origin
        .origin = document.origin(),

        // environment: document's relevant settings object
        .environment = document.relevant_settings_object(),

        // policy container: document's policy container
        .policy_container = document.policy_container(),

        // document: document
        .document = document,

        // FIXME: cryptographic nonce metadata: The current value of el's [[CryptographicNonce]] internal slot
    };

    // 3. If el has an href attribute, then set options's href to the value of el's href attribute.
    if (auto value = attribute(HTML::AttributeNames::href); !value.is_null()) {
        options.href = value;
    }

    // 4. If el has an integrity attribute, then set options's integrity to the value of el's integrity content attribute.
    if (auto value = attribute(HTML::AttributeNames::integrity); !value.is_null()) {
        options.integrity = value;
    }

    // 5. If el has a type attribute, then set options's type to the value of el's type attribute.
    if (auto value = attribute(HTML::AttributeNames::type); !value.is_null()) {
        options.type = value;
    }

    // FIXME: 6. Assert: options's href is not the empty string, or options's source set is not null.

    // 7. Return options.
    return options;
}

// https://html.spec.whatwg.org/multipage/urls-and-fetching.html#create-a-potential-cors-request
static JS::GCPtr<Fetch::Infrastructure::Request> create_potential_cors_request(JS::VM& vm, AK::URL url, Optional<Fetch::Infrastructure::Request::Destination> destination, CORSSettingsAttribute cors_attribute_state, bool same_origin_fallback = false)
{
    // 1. Let mode be "no-cors" if corsAttributeState is No CORS, and "cors" otherwise.
    auto mode = cors_attribute_state == CORSSettingsAttribute::NoCORS
        ? Fetch::Infrastructure::Request::Mode::NoCORS
        : Fetch::Infrastructure::Request::Mode::CORS;

    // 2. If same-origin fallback flag is set and mode is "no-cors", set mode to "same-origin".
    if (same_origin_fallback && mode == Fetch::Infrastructure::Request::Mode::NoCORS)
        mode = Fetch::Infrastructure::Request::Mode::SameOrigin;

    // 3. Let credentialsMode be "include".
    auto credentials_mode = Fetch::Infrastructure::Request::CredentialsMode::Include;

    // 4. If corsAttributeState is Anonymous, set credentialsMode to "same-origin".
    if (cors_attribute_state == CORSSettingsAttribute::Anonymous)
        credentials_mode = Fetch::Infrastructure::Request::CredentialsMode::SameOrigin;

    // 5. Let request be a new request whose URL is url, destination is destination, mode is mode,
    //    credentials mode is credentialsMode, and whose use-URL-credentials flag is set.
    auto request = Fetch::Infrastructure::Request::create(vm);
    request->set_url(move(url));
    request->set_destination(destination);
    request->set_mode(mode);
    request->set_credentials_mode(credentials_mode);
    request->set_use_url_credentials(true);
    return request;
}

// https://html.spec.whatwg.org/multipage/semantics.html#create-a-link-request
JS::GCPtr<Fetch::Infrastructure::Request> HTMLLinkElement::create_link_request(LinkProcessingOptions const& options)
{
    // 1. Assert: options's href is not the empty string.
    VERIFY(!options.href.is_empty());

    // FIXME: 2. If options's destination is not a destination, then return null.

    // 3. Parse a URL given options's href, relative to options's base URL.
    //    If that fails, then return null. Otherwise, let url be the resulting URL record.
    auto url = options.base_url.complete_url(options.href);
    if (!url.is_valid())
        return nullptr;

    // 4. Let request be the result of creating a potential-CORS request given url, options's destination, and options's crossorigin.
    auto request = create_potential_cors_request(vm(), url, options.destination, options.crossorigin);

    // 5. Set request's policy container to options's policy container.
    request->set_policy_container(options.policy_container);

    // 6. Set request's integrity metadata to options's integrity.
    request->set_integrity_metadata(options.integrity);

    // 7. Set request's cryptographic nonce metadata to options's cryptographic nonce metadata.
    request->set_cryptographic_nonce_metadata(options.cryptographic_nonce_metadata);

    // 8. Set request's referrer policy to options's referrer policy.
    request->set_referrer_policy(options.referrer_policy);

    // 9. Set request's client to options's environment.
    request->set_client(&verify_cast<EnvironmentSettingsObject>(options.environment));

    // 10. Return request.
    return request;
}

// https://html.spec.whatwg.org/multipage/semantics.html#default-fetch-and-process-the-linked-resource
void HTMLLinkElement::default_fetch_and_process_the_linked_resource()
{
    // 1. Let options be the result of creating link options from el.
    auto options = create_link_options();

    // 2. Let request be the result of creating a link request given options.
    auto request = create_link_request(options);

    // 3. If request is null, then return.
    if (!request)
        return;

    // FIXME: 4. Set request's synchronous flag.
    // NOTE: The HTML spec currently links to https://fetch.spec.whatwg.org/#synchronous-flag which doesn't go anywhere. :yakthonk:

    // FIXME: 5. Run the linked resource fetch setup steps, given el and request.
    //           If the result is false, then return.

    // 6. Set request's initiator type to "css" if el's rel attribute contains the keyword stylesheet; "link" otherwise.
    if (m_relationship & Relationship::Stylesheet)
        request->set_initiator_type(Fetch::Infrastructure::Request::InitiatorType::CSS);
    else
        request->set_initiator_type(Fetch::Infrastructure::Request::InitiatorType::Link);

    // 7. Fetch request with processResponseConsumeBody set to the following steps
    //    given response response and null, failure, or a byte sequence bodyBytes:
    // FIXME: Handle exceptions from fetch()
    (void)Fetch::Fetching::fetch(*vm().current_realm(), *request,
        Fetch::Infrastructure::FetchAlgorithms::create(vm(),
            {
                .process_request_body_chunk_length = {},
                .process_request_end_of_body = {},
                .process_early_hints_response = {},
                .process_response = {},
                .process_response_end_of_body = {},
                .process_response_consume_body = [this, request](JS::NonnullGCPtr<Fetch::Infrastructure::Response> response, Variant<Empty, Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag, ByteBuffer> body_bytes) {
                    // 1. Let success be true.
                    auto success = true;

                    // 2. If either of the following conditions are met:
                    //    - bodyBytes is null or failure; or
                    //    - response's status is not an ok status,
                    if ((body_bytes.has<Empty>() || body_bytes.has<Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag>())
                        || !Fetch::Infrastructure::is_ok_status(response->status())) {
                        // then set success to false.
                        dbgln("URL: {}, response status: {}", request->url(), response->status());
                        success = false;
                    }

                    // FIXME: 3. Otherwise, wait for the link resource's critical subresources to finish loading.

                    // 4. Process the linked resource given el, success, response, and bodyBytes.
                    process_linked_resource(success, response, move(body_bytes), request->url());
                },
            }));
}

// https://html.spec.whatwg.org/multipage/semantics.html#process-the-linked-resource
void HTMLLinkElement::process_linked_resource(bool success, JS::NonnullGCPtr<Fetch::Infrastructure::Response> response, Variant<Empty, Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag, ByteBuffer> body_bytes, AK::URL request_url)
{
    if (m_relationship & Relationship::Stylesheet)
        process_linked_stylesheet_resource(success, response, move(body_bytes), move(request_url));

    // FIXME: Handle manifest resources.
}

// https://html.spec.whatwg.org/multipage/links.html#link-type-stylesheet:process-the-linked-resource
void HTMLLinkElement::process_linked_stylesheet_resource(bool success, JS::NonnullGCPtr<Fetch::Infrastructure::Response>, Variant<Empty, Fetch::Infrastructure::FetchAlgorithms::ConsumeBodyFailureTag, ByteBuffer> body_bytes, AK::URL request_url)
{
    // FIXME: 1. If the resource's Content-Type metadata is not text/css, then set success to false.

    // FIXME: 2. If el no longer creates an external resource link that contributes to the styling processing model,
    //           or if, since the resource in question was fetched, it has become appropriate to fetch it again, then return.

    // 3. If el has an associated CSS style sheet, remove the CSS style sheet.
    if (m_associated_css_style_sheet)
        remove_a_css_style_sheet(document(), *m_associated_css_style_sheet);

    // 4. If success is true, then:
    if (success) {
        auto sheet = parse_css_stylesheet(CSS::Parser::ParsingContext(document(), request_url), body_bytes.get<ByteBuffer>());

        // FIXME: Make parse_css_stylesheet() return a JS::NonnullGCPtr
        VERIFY(sheet);

        m_associated_css_style_sheet = sheet;

        // 1. Create a CSS style sheet with the following properties:
        // type: text/css
        // location: The resulting URL string determined during the fetch and process the linked resource algorithm.
        // owner node: element
        // media: The media attribute of element.
        // title: The title attribute of element, if element is in a document tree, or the empty string otherwise.
        // FIXME: alternate flag: Set if the link is an alternative style sheet and element's explicitly enabled is false; unset otherwise.
        // FIXME: origin-clean flag: Set if the resource is CORS-same-origin; unset otherwise
        // parent CSS style sheet: null
        // owner CSS rule: null
        // disabled flag: Left at its default value.
        // CSS rules: Left uninitialized.
        auto title = in_a_document_tree() ? attribute(HTML::AttributeNames::title) : "";
        create_a_css_style_sheet(document(), "text/css", this, attribute(HTML::AttributeNames::media), title, false, true, request_url.to_deprecated_string(), nullptr, nullptr, *sheet);

        // FIXME: The CSS environment encoding is the result of running the following steps: [CSSSYNTAX]

        // 2. Fire an event named load at el.
        dispatch_event(*DOM::Event::create(realm(), HTML::EventNames::load));
    } else {
        // 5. Otherwise, fire an event named error at el.
        dispatch_event(*DOM::Event::create(realm(), HTML::EventNames::error));
    }

    // FIXME: 6. If el contributes a script-blocking style sheet, then:

    // FIXME: 7. Unblock rendering on el.
}

void HTMLLinkElement::inserted()
{
    if (has_attribute(AttributeNames::disabled) && (m_relationship & Relationship::Stylesheet))
        return;

    HTMLElement::inserted();

    if (m_relationship & Relationship::Stylesheet && !(m_relationship & Relationship::Alternate)) {
        // FIXME: Delay the load event
        fetch_and_process_the_linked_resource();
    }

    if (m_relationship & Relationship::Preload) {
        // FIXME: Respect the "as" attribute.
        LoadRequest request;
        request.set_url(document().parse_url(attribute(HTML::AttributeNames::href)));
        m_preload_resource = ResourceLoader::the().load_resource(Resource::Type::Generic, request);
    } else if (m_relationship & Relationship::DNSPrefetch) {
        ResourceLoader::the().prefetch_dns(document().parse_url(attribute(HTML::AttributeNames::href)));
    } else if (m_relationship & Relationship::Preconnect) {
        ResourceLoader::the().preconnect(document().parse_url(attribute(HTML::AttributeNames::href)));
    } else if (m_relationship & Relationship::Icon) {
        auto favicon_url = document().parse_url(href());
        auto favicon_request = LoadRequest::create_for_url_on_page(favicon_url, document().page());
        set_resource(ResourceLoader::the().load_resource(Resource::Type::Generic, favicon_request));
    }
}

bool HTMLLinkElement::has_loaded_icon() const
{
    return m_relationship & Relationship::Icon && resource() && resource()->is_loaded() && resource()->has_encoded_data();
}

void HTMLLinkElement::parse_attribute(FlyString const& name, DeprecatedString const& value)
{
    // 4.6.7 Link types - https://html.spec.whatwg.org/multipage/links.html#linkTypes
    if (name == HTML::AttributeNames::rel) {
        m_relationship = 0;
        // Keywords are always ASCII case-insensitive, and must be compared as such.
        auto lowercased_value = value.to_lowercase();
        // To determine which link types apply to a link, a, area, or form element,
        // the element's rel attribute must be split on ASCII whitespace.
        // The resulting tokens are the keywords for the link types that apply to that element.
        auto parts = lowercased_value.split_view(Infra::is_ascii_whitespace);
        for (auto& part : parts) {
            if (part == "stylesheet"sv)
                m_relationship |= Relationship::Stylesheet;
            else if (part == "alternate"sv)
                m_relationship |= Relationship::Alternate;
            else if (part == "preload"sv)
                m_relationship |= Relationship::Preload;
            else if (part == "dns-prefetch"sv)
                m_relationship |= Relationship::DNSPrefetch;
            else if (part == "preconnect"sv)
                m_relationship |= Relationship::Preconnect;
            else if (part == "icon"sv)
                m_relationship |= Relationship::Icon;
        }
    }
}

void HTMLLinkElement::resource_did_fail()
{
    dbgln_if(CSS_LOADER_DEBUG, "HTMLLinkElement: Resource did fail. URL: {}", resource()->url());

    m_document_load_event_delayer.clear();
}

void HTMLLinkElement::resource_did_load()
{
    VERIFY(resource());
    VERIFY(m_relationship & Relationship::Icon);

    if (m_relationship & Relationship::Icon)
        resource_did_load_favicon();
}

void HTMLLinkElement::did_remove_attribute(FlyString const& attribute_name)
{
    HTMLElement::did_remove_attribute(attribute_name);
    if (attribute_name == HTML::AttributeNames::disabled) {
        document().invalidate_style();
        document().style_computer().invalidate_rule_cache();
    }
}

void HTMLLinkElement::resource_did_load_favicon()
{
    VERIFY(m_relationship & (Relationship::Icon));
    if (!resource()->has_encoded_data()) {
        dbgln_if(SPAM_DEBUG, "Favicon downloaded, no encoded data");
        return;
    }

    dbgln_if(SPAM_DEBUG, "Favicon downloaded, {} bytes from {}", resource()->encoded_data().size(), resource()->url());

    document().check_favicon_after_loading_link_resource();
}

bool HTMLLinkElement::load_favicon_and_use_if_window_is_active()
{
    if (!has_loaded_icon())
        return false;

    RefPtr<Gfx::Bitmap> favicon_bitmap;
    auto decoded_image = Platform::ImageCodecPlugin::the().decode_image(resource()->encoded_data());
    if (!decoded_image.has_value() || decoded_image->frames.is_empty()) {
        dbgln("Could not decode favicon {}", resource()->url());
        return false;
    }

    favicon_bitmap = decoded_image->frames[0].bitmap;
    dbgln_if(IMAGE_DECODER_DEBUG, "Decoded favicon, {}", favicon_bitmap->size());

    auto* page = document().page();
    if (!page)
        return favicon_bitmap;

    if (document().browsing_context() == &page->top_level_browsing_context())
        if (favicon_bitmap) {
            page->client().page_did_change_favicon(*favicon_bitmap);
            return true;
        }

    return false;
}

void HTMLLinkElement::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_associated_css_style_sheet);
}
}
