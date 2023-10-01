/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Bytecode/Executable.h>
#include <LibJS/SourceCode.h>

namespace JS::Bytecode {

Executable::Executable(NonnullOwnPtr<IdentifierTable> identifier_table, NonnullOwnPtr<StringTable> string_table, NonnullOwnPtr<RegexTable> regex_table, NonnullRefPtr<SourceCode const> source_code)
    : string_table(move(string_table))
    , identifier_table(move(identifier_table))
    , regex_table(move(regex_table))
    , source_code(move(source_code))
{
}

Executable::~Executable() = default;

void Executable::dump() const
{
    dbgln("\033[33;1mJS::Bytecode::Executable\033[0m ({})", name);
    for (auto& block : basic_blocks)
        block->dump(*this);
    if (!string_table->is_empty()) {
        outln();
        string_table->dump();
    }
    if (!identifier_table->is_empty()) {
        outln();
        identifier_table->dump();
    }
}

}
