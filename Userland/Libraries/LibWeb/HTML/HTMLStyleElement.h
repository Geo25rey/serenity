/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/HTML/HTMLElement.h>

namespace Web::HTML {

class HTMLStyleElement final : public HTMLElement {
    WEB_PLATFORM_OBJECT(HTMLStyleElement, HTMLElement);

public:
    virtual ~HTMLStyleElement() override;

    virtual void children_changed() override;
    virtual void inserted() override;
    virtual void removed_from(Node*) override;

    void update_a_style_block();

    CSS::CSSStyleSheet* sheet();
    CSS::CSSStyleSheet const* sheet() const;

    bool disabled() const;
    void set_disabled(bool);

private:
    HTMLStyleElement(DOM::Document&, DOM::QualifiedName);

    virtual void visit_edges(Cell::Visitor&) override;

    // https://www.w3.org/TR/cssom/#associated-css-style-sheet
    JS::GCPtr<CSS::CSSStyleSheet> m_associated_css_style_sheet;
};

void create_a_css_style_sheet(DOM::Document&, DeprecatedString type, DOM::Element* owner_node, DeprecatedString media, DeprecatedString title, bool alternate, bool origin_clean, DeprecatedString location, CSS::CSSStyleSheet* parent_style_sheet, CSS::CSSRule* owner_rule, CSS::CSSStyleSheet&);
void remove_a_css_style_sheet(DOM::Document&, CSS::CSSStyleSheet&);

}
