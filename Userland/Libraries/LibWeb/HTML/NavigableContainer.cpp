/*
 * Copyright (c) 2020-2022, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Requests.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/BrowsingContextGroup.h>
#include <LibWeb/HTML/DocumentState.h>
#include <LibWeb/HTML/HTMLIFrameElement.h>
#include <LibWeb/HTML/Navigable.h>
#include <LibWeb/HTML/NavigableContainer.h>
#include <LibWeb/HTML/NavigationParams.h>
#include <LibWeb/HTML/Origin.h>
#include <LibWeb/HTML/Scripting/WindowEnvironmentSettingsObject.h>
#include <LibWeb/HighResolutionTime/TimeOrigin.h>
#include <LibWeb/Page/Page.h>

namespace Web::HTML {

HashTable<NavigableContainer*>& NavigableContainer::all_instances()
{
    static HashTable<NavigableContainer*> set;
    return set;
}

NavigableContainer::NavigableContainer(DOM::Document& document, DOM::QualifiedName qualified_name)
    : HTMLElement(document, move(qualified_name))
{
}

NavigableContainer::~NavigableContainer() = default;

void NavigableContainer::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_nested_browsing_context);
}

struct BrowsingContextAndDocument {
    JS::NonnullGCPtr<BrowsingContext> browsing_context;
    JS::NonnullGCPtr<DOM::Document> document;
};

// https://html.spec.whatwg.org/multipage/document-sequences.html#creating-a-new-browsing-context
static BrowsingContextAndDocument create_new_browsing_context_and_document(Page& page, JS::GCPtr<DOM::Document> creator, JS::GCPtr<DOM::Element> embedder, JS::NonnullGCPtr<BrowsingContextGroup> group)
{
    auto& vm = group->vm();

    // 1. Let browsingContext be a new browsing context.
    JS::NonnullGCPtr<BrowsingContext> browsing_context = *vm.heap().allocate_without_realm<BrowsingContext>(page, nullptr);

    // 2. Let unsafeContextCreationTime be the unsafe shared current time.
    [[maybe_unused]] auto unsafe_context_creation_time = HighResolutionTime::unsafe_shared_current_time();

    // 3. Let creatorOrigin be null.
    Optional<HTML::Origin> creator_origin = {};

    // 4. If creator is non-null, then:
    if (creator) {
        // 1. Set creatorOrigin to creator's origin.
        creator_origin = creator->origin();

        // FIXME: 2. Set browsingContext's creator base URL to an algorithm which returns creator's base URL.

        // FIXME: 3. Set browsingContext's virtual browsing context group ID to creator's browsing context's top-level browsing context's virtual browsing context group ID.
    }

    // FIXME: 5. Let sandboxFlags be the result of determining the creation sandboxing flags given browsingContext and embedder.
    SandboxingFlagSet sandbox_flags;

    // 6. Let origin be the result of determining the origin given about:blank, sandboxFlags, creatorOrigin, and null.
    auto origin = determine_the_origin(AK::URL("about:blank"sv), sandbox_flags, creator_origin, {});

    // FIXME: 7. Let permissionsPolicy be the result of creating a permissions policy given browsingContext and origin. [PERMISSIONSPOLICY]

    // FIXME: 8. Let agent be the result of obtaining a similar-origin window agent given origin, group, and false.

    JS::GCPtr<Window> window;

    // 9. Let realm execution context be the result of creating a new JavaScript realm given agent and the following customizations:
    auto realm_execution_context = Bindings::create_a_new_javascript_realm(
        Bindings::main_thread_vm(),
        [&](JS::Realm& realm) -> JS::Object* {
            browsing_context->set_window_proxy(*realm.heap().allocate<WindowProxy>(realm, realm));

            // - For the global object, create a new Window object.
            window = HTML::Window::create(realm);
            return window.ptr();
        },
        [&](JS::Realm&) -> JS::Object* {
            // - For the global this binding, use browsingContext's WindowProxy object.
            return browsing_context->window_proxy();
        });

    // 10. Let topLevelCreationURL be about:blank if embedder is null; otherwise embedder's relevant settings object's top-level creation URL.
    auto top_level_creation_url = !embedder ? AK::URL("about:blank") : relevant_settings_object(*embedder).top_level_creation_url;

    // 11. Let topLevelOrigin be origin if embedder is null; otherwise embedder's relevant settings object's top-level origin.
    auto top_level_origin = !embedder ? origin : relevant_settings_object(*embedder).origin();

    // 12. Set up a window environment settings object with about:blank, realm execution context, null, topLevelCreationURL, and topLevelOrigin.
    HTML::WindowEnvironmentSettingsObject::setup(
        AK::URL("about:blank"),
        move(realm_execution_context),
        {},
        top_level_creation_url,
        top_level_origin);

    // 13. Let loadTimingInfo be a new document load timing info with its navigation start time set to the result of calling
    //     coarsen time with unsafeContextCreationTime and the new environment settings object's cross-origin isolated capability.
    auto load_timing_info = DOM::DocumentLoadTimingInfo();
    load_timing_info.navigation_start_time = HighResolutionTime::coarsen_time(
        unsafe_context_creation_time,
        verify_cast<WindowEnvironmentSettingsObject>(Bindings::host_defined_environment_settings_object(window->realm())).cross_origin_isolated_capability() == CanUseCrossOriginIsolatedAPIs::Yes);

    // 14. Let document be a new Document, with:
    auto document = DOM::Document::create(window->realm());

    // Non-standard
    document->set_window(*window);
    window->set_associated_document(*document);

    // type: "html"
    document->set_document_type(DOM::Document::Type::HTML);

    // content type: "text/html"
    document->set_content_type("text/html");

    // mode: "quirks"
    document->set_quirks_mode(DOM::QuirksMode::Yes);

    // origin: origin
    document->set_origin(origin);

    // browsing context: browsingContext
    document->set_browsing_context(browsing_context);

    // FIXME: permissions policy: permissionsPolicy

    // FIXME: active sandboxing flag set: sandboxFlags

    // load timing info: loadTimingInfo
    document->set_load_timing_info(load_timing_info);

    // is initial about:blank: true
    document->set_is_initial_about_blank(true);

    // 15. If creator is non-null, then:
    if (creator) {
        // 1. Set document's referrer to the serialization of creator's URL.
        document->set_referrer(creator->url().serialize());

        // FIXME: 2. Set document's policy container to a clone of creator's policy container.

        // 3. If creator's origin is same origin with creator's relevant settings object's top-level origin,
        if (creator->origin().is_same_origin(creator->relevant_settings_object().top_level_origin)) {
            // then set document's cross-origin opener policy to creator's browsing context's top-level browsing context's active document's cross-origin opener policy.
            VERIFY(creator->browsing_context());
            VERIFY(creator->browsing_context()->top_level_browsing_context().active_document());
            document->set_cross_origin_opener_policy(creator->browsing_context()->top_level_browsing_context().active_document()->cross_origin_opener_policy());
        }
    }

    // 16. Assert: document's URL and document's relevant settings object's creation URL are about:blank.
    VERIFY(document->url() == "about:blank"sv);
    VERIFY(document->relevant_settings_object().creation_url == "about:blank"sv);

    // 17. Mark document as ready for post-load tasks.
    document->set_ready_for_post_load_tasks(true);

    // 18. Ensure that document has a single child html node, which itself has two empty child nodes: a head element, and a body element.
    auto html_node = document->create_element(HTML::TagNames::html).release_value();
    MUST(html_node->append_child(document->create_element(HTML::TagNames::head).release_value()));
    MUST(html_node->append_child(document->create_element(HTML::TagNames::body).release_value()));
    MUST(document->append_child(html_node));

    // 19. Make active document.
    document->make_active();

    // 20. Completely finish loading document.
    document->completely_finish_loading();

    // 21. Return browsingContext and document.
    return { browsing_context, document };
}

// https://html.spec.whatwg.org/multipage/document-sequences.html#create-a-new-nested-navigable
void NavigableContainer::create_new_nested_navigable()
{
    // 1. Let parentNavigable be element's node navigable.
    auto parent_navigable = node_navigable();

    // 2. Let group be element's node document's browsing context's top-level browsing context's group.
    VERIFY(document().browsing_context());
    auto group = document().browsing_context()->top_level_browsing_context().group();
    VERIFY(group);

    // 3. Let browsingContext and document be the result of creating a new browsing context and document given element's node document, element, and group.
    auto* page = document().page();
    VERIFY(page);
    auto [browsing_context, document] = create_new_browsing_context_and_document(*page, this->document(), *this, *group);

    // 4. Let targetName be null.
    Optional<DeprecatedString> target_name;

    // 5. If element has a name content attribute, then set targetName to the value of that attribute.
    if (auto value = attribute(HTML::AttributeNames::name); !value.is_null())
        target_name = value;

    // 6. Let documentState be a new document state, with
    JS::NonnullGCPtr<DocumentState> document_state = *heap().allocate_without_realm<HTML::DocumentState>();

    // 7. Let navigable be a new navigable.
    JS::NonnullGCPtr<Navigable> navigable = *heap().allocate_without_realm<Navigable>();

    // 8. Initialize the navigable navigable given documentState and parentNavigable.
    navigable->initialize_navigable(document_state, parent_navigable);

    // 9. Set element's nested navigable to navigable.
    m_nested_navigable = navigable;

    // 10. Let historyEntry be navigable's active session history entry.
    auto history_entry = navigable->active_session_history_entry();

    // 11. Let traversable be parentNavigable's traversable navigable.
    auto traversable = parent_navigable->traversable_navigable();

    // FIXME: 12. Append the following session history traversal steps to traversable:
    // 1. Let parentDocState be parentNavigable's active session history entry's document state.
    // 2. Let targetStepSHE be the first session history entry in traversable's session history entries whose document state equals parentDocState.
    // 3. Set historyEntry's step to targetStepSHE's step.
    // 4. Let nestedHistory be a new nested history whose id is navigable's id and entries list is « historyEntry ».
    // 5. Append nestedHistory to parentDocState's nested histories.
    // 6. Apply pending history changes to traversable.

    (void)document;
    (void)browsing_context;
    (void)history_entry;
    (void)traversable;
}

// https://html.spec.whatwg.org/multipage/browsers.html#creating-a-new-nested-browsing-context
void NavigableContainer::create_new_nested_browsing_context()
{
    // 1. Let group be element's node document's browsing context's top-level browsing context's group.
    VERIFY(document().browsing_context());
    auto* group = document().browsing_context()->top_level_browsing_context().group();

    // NOTE: The spec assumes that `group` is non-null here.
    VERIFY(group);
    VERIFY(group->page());

    // 2. Let browsingContext be the result of creating a new browsing context with element's node document, element, and group.
    // 3. Set element's nested browsing context to browsingContext.
    m_nested_browsing_context = BrowsingContext::create_a_new_browsing_context(*group->page(), document(), *this, *group);

    document().browsing_context()->append_child(*m_nested_browsing_context);
    m_nested_browsing_context->set_frame_nesting_levels(document().browsing_context()->frame_nesting_levels());
    m_nested_browsing_context->register_frame_nesting(document().url());

    // 4. If element has a name attribute, then set browsingContext's name to the value of this attribute.
    if (auto name = attribute(HTML::AttributeNames::name); !name.is_empty())
        m_nested_browsing_context->set_name(name);
}

// https://html.spec.whatwg.org/multipage/browsers.html#concept-bcc-content-document
const DOM::Document* NavigableContainer::content_document() const
{
    // 1. If container's nested browsing context is null, then return null.
    if (m_nested_browsing_context == nullptr)
        return nullptr;

    // 2. Let context be container's nested browsing context.
    auto const& context = *m_nested_browsing_context;

    // 3. Let document be context's active document.
    auto const* document = context.active_document();

    // FIXME: This should not be here, as we're expected to have a document at this point.
    if (!document)
        return nullptr;

    VERIFY(document);
    VERIFY(m_document);

    // 4. If document's origin and container's node document's origin are not same origin-domain, then return null.
    if (!document->origin().is_same_origin_domain(m_document->origin()))
        return nullptr;

    // 5. Return document.
    return document;
}

DOM::Document const* NavigableContainer::content_document_without_origin_check() const
{
    if (!m_nested_browsing_context)
        return nullptr;
    return m_nested_browsing_context->active_document();
}

// https://html.spec.whatwg.org/multipage/embedded-content-other.html#dom-media-getsvgdocument
const DOM::Document* NavigableContainer::get_svg_document() const
{
    // 1. Let document be this element's content document.
    auto const* document = content_document();

    // 2. If document is non-null and was created by the page load processing model for XML files section because the computed type of the resource in the navigate algorithm was image/svg+xml, then return document.
    if (document && document->content_type() == "image/svg+xml"sv)
        return document;
    // 3. Return null.
    return nullptr;
}

HTML::WindowProxy* NavigableContainer::content_window()
{
    if (!m_nested_browsing_context)
        return nullptr;
    return m_nested_browsing_context->window_proxy();
}

// https://html.spec.whatwg.org/multipage/urls-and-fetching.html#matches-about:blank
static bool url_matches_about_blank(AK::URL const& url)
{
    // A URL matches about:blank if its scheme is "about", its path contains a single string "blank", its username and password are the empty string, and its host is null.
    return url.scheme() == "about"sv
        && url.path() == "blank"sv
        && url.username().is_empty()
        && url.password().is_empty()
        && url.host().is_null();
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#shared-attribute-processing-steps-for-iframe-and-frame-elements
void NavigableContainer::shared_attribute_processing_steps_for_iframe_and_frame(bool initial_insertion)
{
    // 1. Let url be the URL record about:blank.
    auto url = AK::URL("about:blank");

    // 2. If element has a src attribute specified, and its value is not the empty string,
    //    then parse the value of that attribute relative to element's node document.
    //    If this is successful, then set url to the resulting URL record.
    auto src_attribute_value = attribute(HTML::AttributeNames::src);
    if (!src_attribute_value.is_null() && !src_attribute_value.is_empty()) {
        auto parsed_src = document().parse_url(src_attribute_value);
        if (parsed_src.is_valid())
            url = parsed_src;
    }

    // 3. If there exists an ancestor browsing context of element's nested browsing context
    //    whose active document's URL, ignoring fragments, is equal to url, then return.
    if (m_nested_browsing_context) {
        for (auto ancestor = m_nested_browsing_context->parent(); ancestor; ancestor = ancestor->parent()) {
            VERIFY(ancestor->active_document());
            if (ancestor->active_document()->url().equals(url, AK::URL::ExcludeFragment::Yes))
                return;
        }
    }

    // 4. If url matches about:blank and initialInsertion is true, then:
    if (url_matches_about_blank(url) && initial_insertion) {
        // FIXME: 1. Perform the URL and history update steps given element's nested browsing context's active document and url.

        // 2. Run the iframe load event steps given element.
        // FIXME: The spec doesn't check frame vs iframe here. Bug: https://github.com/whatwg/html/issues/8295
        if (is<HTMLIFrameElement>(*this)) {
            run_iframe_load_event_steps(static_cast<HTMLIFrameElement&>(*this));
        }

        // 3. Return.
        return;
    }

    // 5. Let resource be a new request whose URL is url and whose referrer policy is the current state of element's referrerpolicy content attribute.
    auto resource = Fetch::Infrastructure::Request::create(vm());
    resource->set_url(url);
    // FIXME: Set the referrer policy.

    // AD-HOC:
    if (url.scheme() == "file" && document().origin().scheme() != "file") {
        dbgln("iframe failed to load URL: Security violation: {} may not load {}", document().url(), url);
        return;
    }

    // 6. If element is an iframe element, then set element's current navigation was lazy loaded boolean to false.
    if (is<HTMLIFrameElement>(*this)) {
        static_cast<HTMLIFrameElement&>(*this).set_current_navigation_was_lazy_loaded(false);
    }

    // 7. If element is an iframe element, and the will lazy load element steps given element return true, then:
    if (is<HTMLIFrameElement>(*this) && static_cast<HTMLIFrameElement&>(*this).will_lazy_load_element()) {
        // FIXME: 1. Set element's lazy load resumption steps to the rest of this algorithm starting with the step labeled navigate to the resource.
        // FIXME: 2. Set element's current navigation was lazy loaded boolean to true.
        // FIXME: 3. Start intersection-observing a lazy loading element for element.
        // FIXME: 4. Return.
    }

    // 8. Navigate to the resource: navigate an iframe or frame given element and resource.
    navigate_an_iframe_or_frame(resource);
}

// https://html.spec.whatwg.org/multipage/iframe-embed-object.html#navigate-an-iframe-or-frame
void NavigableContainer::navigate_an_iframe_or_frame(JS::NonnullGCPtr<Fetch::Infrastructure::Request> resource)
{
    // 1. Let historyHandling be "default".
    auto history_handling = HistoryHandlingBehavior::Default;

    // 2. If element's nested browsing context's active document is not completely loaded, then set historyHandling to "replace".
    VERIFY(m_nested_browsing_context);
    VERIFY(m_nested_browsing_context->active_document());
    if (!m_nested_browsing_context->active_document()->is_completely_loaded()) {
        history_handling = HistoryHandlingBehavior::Replace;
    }

    // FIXME: 3. Let reportFrameTiming be the following step given response response:
    //           queue an element task on the networking task source
    //           given element's node document's relevant global object
    //           to finalize and report timing given response, element's node document's relevant global object, and element's local name.

    // 4. Navigate element's nested browsing context to resource,
    //    with historyHandling set to historyHandling,
    //    the source browsing context set to element's node document's browsing context,
    //    FIXME: and processResponseEndOfBody set to reportFrameTiming.
    auto* source_browsing_context = document().browsing_context();
    VERIFY(source_browsing_context);
    MUST(m_nested_browsing_context->navigate(resource, *source_browsing_context, false, history_handling));
}

}
