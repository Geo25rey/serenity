/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/AST.h>
#include <LibJS/Bytecode/BasicBlock.h>
#include <LibJS/Bytecode/Generator.h>
#include <LibJS/Bytecode/Instruction.h>
#include <LibJS/Bytecode/Op.h>
#include <LibJS/Bytecode/Register.h>

namespace JS::Bytecode {

Generator::Generator()
    : m_string_table(make<StringTable>())
    , m_identifier_table(make<IdentifierTable>())
{
}

CodeGenerationErrorOr<NonnullOwnPtr<Executable>> Generator::generate(ASTNode const& node, FunctionKind enclosing_function_kind)
{
    Generator generator;
    generator.switch_to_basic_block(generator.make_block());
    generator.m_enclosing_function_kind = enclosing_function_kind;
    if (generator.is_in_generator_or_async_function()) {
        // Immediately yield with no value.
        auto& start_block = generator.make_block();
        generator.emit<Bytecode::Op::Yield>(Label { start_block });
        generator.switch_to_basic_block(start_block);
        // NOTE: This doesn't have to handle received throw/return completions, as GeneratorObject::resume_abrupt
        //       will not enter the generator from the SuspendedStart state and immediately completes the generator.
    }
    TRY(node.generate_bytecode(generator));
    if (generator.is_in_generator_or_async_function()) {
        // Terminate all unterminated blocks with yield return
        for (auto& block : generator.m_root_basic_blocks) {
            if (block->is_terminated())
                continue;
            generator.switch_to_basic_block(*block);
            generator.emit<Bytecode::Op::LoadImmediate>(js_undefined());
            generator.emit<Bytecode::Op::Yield>(nullptr);
        }
    }

    bool is_strict_mode = false;
    if (is<Program>(node))
        is_strict_mode = static_cast<Program const&>(node).is_strict_mode();
    else if (is<FunctionBody>(node))
        is_strict_mode = static_cast<FunctionBody const&>(node).in_strict_mode();
    else if (is<FunctionDeclaration>(node))
        is_strict_mode = static_cast<FunctionDeclaration const&>(node).is_strict_mode();
    else if (is<FunctionExpression>(node))
        is_strict_mode = static_cast<FunctionExpression const&>(node).is_strict_mode();

    return adopt_own(*new Executable {
        .name = {},
        .basic_blocks = move(generator.m_root_basic_blocks),
        .string_table = move(generator.m_string_table),
        .identifier_table = move(generator.m_identifier_table),
        .number_of_registers = generator.m_next_register,
        .is_strict_mode = is_strict_mode,
    });
}

void Generator::grow(size_t additional_size)
{
    VERIFY(m_current_basic_block);
    m_current_basic_block->grow(additional_size);
}

void* Generator::next_slot()
{
    VERIFY(m_current_basic_block);
    return m_current_basic_block->next_slot();
}

Register Generator::allocate_register()
{
    VERIFY(m_next_register != NumericLimits<u32>::max());
    return Register { m_next_register++ };
}

Label Generator::nearest_continuable_scope() const
{
    return m_continuable_scopes.last().bytecode_target;
}

void Generator::block_declaration_instantiation(ScopeNode const& scope_node)
{
    start_boundary(BlockBoundaryType::LeaveLexicalEnvironment);
    emit<Bytecode::Op::BlockDeclarationInstantiation>(scope_node);
}

void Generator::begin_variable_scope()
{
    start_boundary(BlockBoundaryType::LeaveLexicalEnvironment);
    emit<Bytecode::Op::CreateLexicalEnvironment>();
}

void Generator::end_variable_scope()
{
    end_boundary(BlockBoundaryType::LeaveLexicalEnvironment);

    if (!m_current_basic_block->is_terminated()) {
        emit<Bytecode::Op::LeaveLexicalEnvironment>();
    }
}

void Generator::begin_continuable_scope(Label continue_target, Vector<DeprecatedFlyString> const& language_label_set)
{
    m_continuable_scopes.append({ continue_target, language_label_set });
    start_boundary(BlockBoundaryType::Continue);
}

void Generator::end_continuable_scope()
{
    m_continuable_scopes.take_last();
    end_boundary(BlockBoundaryType::Continue);
}

Label Generator::nearest_breakable_scope() const
{
    return m_breakable_scopes.last().bytecode_target;
}

void Generator::begin_breakable_scope(Label breakable_target, Vector<DeprecatedFlyString> const& language_label_set)
{
    m_breakable_scopes.append({ breakable_target, language_label_set });
    start_boundary(BlockBoundaryType::Break);
}

void Generator::end_breakable_scope()
{
    m_breakable_scopes.take_last();
    end_boundary(BlockBoundaryType::Break);
}

CodeGenerationErrorOr<void> Generator::emit_load_from_reference(JS::ASTNode const& node)
{
    if (is<Identifier>(node)) {
        auto& identifier = static_cast<Identifier const&>(node);
        emit<Bytecode::Op::GetVariable>(intern_identifier(identifier.string()));
        return {};
    }
    if (is<MemberExpression>(node)) {
        auto& expression = static_cast<MemberExpression const&>(node);

        // https://tc39.es/ecma262/#sec-super-keyword-runtime-semantics-evaluation
        if (is<SuperExpression>(expression.object())) {
            // 1. Let env be GetThisEnvironment().
            // 2. Let actualThis be ? env.GetThisBinding().
            // NOTE: Whilst this isn't used, it's still observable (e.g. it throws if super() hasn't been called)
            emit<Bytecode::Op::ResolveThisBinding>();

            Optional<Bytecode::Register> computed_property_value_register;

            if (expression.is_computed()) {
                // SuperProperty : super [ Expression ]
                // 3. Let propertyNameReference be ? Evaluation of Expression.
                // 4. Let propertyNameValue be ? GetValue(propertyNameReference).
                TRY(expression.property().generate_bytecode(*this));
                computed_property_value_register = allocate_register();
                emit<Bytecode::Op::Store>(*computed_property_value_register);
            }

            // 5/7. Return ? MakeSuperPropertyReference(actualThis, propertyKey, strict).

            // https://tc39.es/ecma262/#sec-makesuperpropertyreference
            // 1. Let env be GetThisEnvironment().
            // 2. Assert: env.HasSuperBinding() is true.
            // 3. Let baseValue be ? env.GetSuperBase().
            emit<Bytecode::Op::ResolveSuperBase>();

            // 4. Return the Reference Record { [[Base]]: baseValue, [[ReferencedName]]: propertyKey, [[Strict]]: strict, [[ThisValue]]: actualThis }.
            if (computed_property_value_register.has_value()) {
                // 5. Let propertyKey be ? ToPropertyKey(propertyNameValue).
                // FIXME: This does ToPropertyKey out of order, which is observable by Symbol.toPrimitive!
                auto super_base_register = allocate_register();
                emit<Bytecode::Op::Store>(super_base_register);
                emit<Bytecode::Op::Load>(*computed_property_value_register);
                emit<Bytecode::Op::GetByValue>(super_base_register);
            } else {
                // 3. Let propertyKey be StringValue of IdentifierName.
                auto identifier_table_ref = intern_identifier(verify_cast<Identifier>(expression.property()).string());
                emit<Bytecode::Op::GetById>(identifier_table_ref);
            }
        } else {
            TRY(expression.object().generate_bytecode(*this));

            if (expression.is_computed()) {
                auto object_reg = allocate_register();
                emit<Bytecode::Op::Store>(object_reg);

                TRY(expression.property().generate_bytecode(*this));
                emit<Bytecode::Op::GetByValue>(object_reg);
            } else if (expression.property().is_identifier()) {
                auto identifier_table_ref = intern_identifier(verify_cast<Identifier>(expression.property()).string());
                emit<Bytecode::Op::GetById>(identifier_table_ref);
            } else if (expression.property().is_private_identifier()) {
                auto identifier_table_ref = intern_identifier(verify_cast<PrivateIdentifier>(expression.property()).string());
                emit<Bytecode::Op::GetPrivateById>(identifier_table_ref);
            } else {
                return CodeGenerationError {
                    &expression,
                    "Unimplemented non-computed member expression"sv
                };
            }
        }
        return {};
    }
    VERIFY_NOT_REACHED();
}

CodeGenerationErrorOr<void> Generator::emit_store_to_reference(JS::ASTNode const& node)
{
    if (is<Identifier>(node)) {
        auto& identifier = static_cast<Identifier const&>(node);
        emit<Bytecode::Op::SetVariable>(intern_identifier(identifier.string()));
        return {};
    }
    if (is<MemberExpression>(node)) {
        // NOTE: The value is in the accumulator, so we have to store that away first.
        auto value_reg = allocate_register();
        emit<Bytecode::Op::Store>(value_reg);

        auto& expression = static_cast<MemberExpression const&>(node);
        TRY(expression.object().generate_bytecode(*this));

        auto object_reg = allocate_register();
        emit<Bytecode::Op::Store>(object_reg);

        if (expression.is_computed()) {
            TRY(expression.property().generate_bytecode(*this));
            auto property_reg = allocate_register();
            emit<Bytecode::Op::Store>(property_reg);
            emit<Bytecode::Op::Load>(value_reg);
            emit<Bytecode::Op::PutByValue>(object_reg, property_reg);
        } else if (expression.property().is_identifier()) {
            emit<Bytecode::Op::Load>(value_reg);
            auto identifier_table_ref = intern_identifier(verify_cast<Identifier>(expression.property()).string());
            emit<Bytecode::Op::PutById>(object_reg, identifier_table_ref);
        } else if (expression.property().is_private_identifier()) {
            emit<Bytecode::Op::Load>(value_reg);
            auto identifier_table_ref = intern_identifier(verify_cast<PrivateIdentifier>(expression.property()).string());
            emit<Bytecode::Op::PutPrivateById>(object_reg, identifier_table_ref);
        } else {
            return CodeGenerationError {
                &expression,
                "Unimplemented non-computed member expression"sv
            };
        }
        return {};
    }

    return CodeGenerationError {
        &node,
        "Unimplemented/invalid node used a reference"sv
    };
}

CodeGenerationErrorOr<void> Generator::emit_delete_reference(JS::ASTNode const& node)
{
    if (is<Identifier>(node)) {
        auto& identifier = static_cast<Identifier const&>(node);
        emit<Bytecode::Op::DeleteVariable>(intern_identifier(identifier.string()));
        return {};
    }

    if (is<MemberExpression>(node)) {
        auto& expression = static_cast<MemberExpression const&>(node);
        TRY(expression.object().generate_bytecode(*this));

        if (expression.is_computed()) {
            auto object_reg = allocate_register();
            emit<Bytecode::Op::Store>(object_reg);

            TRY(expression.property().generate_bytecode(*this));
            emit<Bytecode::Op::DeleteByValue>(object_reg);
        } else if (expression.property().is_identifier()) {
            auto identifier_table_ref = intern_identifier(verify_cast<Identifier>(expression.property()).string());
            emit<Bytecode::Op::DeleteById>(identifier_table_ref);
        } else {
            // NOTE: Trying to delete a private field generates a SyntaxError in the parser.
            return CodeGenerationError {
                &expression,
                "Unimplemented non-computed member expression"sv
            };
        }
        return {};
    }

    // Though this will have no deletion effect, we still have to evaluate the node as it can have side effects.
    // For example: delete a(); delete ++c.b; etc.

    // 13.5.1.2 Runtime Semantics: Evaluation, https://tc39.es/ecma262/#sec-delete-operator-runtime-semantics-evaluation
    // 1. Let ref be the result of evaluating UnaryExpression.
    // 2. ReturnIfAbrupt(ref).
    TRY(node.generate_bytecode(*this));

    // 3. If ref is not a Reference Record, return true.
    emit<Bytecode::Op::LoadImmediate>(Value(true));

    // NOTE: The rest of the steps are handled by Delete{Variable,ByValue,Id}.
    return {};
}

void Generator::generate_break()
{
    bool last_was_finally = false;
    // FIXME: Reduce code duplication
    for (size_t i = m_boundaries.size(); i > 0; --i) {
        auto boundary = m_boundaries[i - 1];
        using enum BlockBoundaryType;
        switch (boundary) {
        case Break:
            emit<Op::Jump>().set_targets(nearest_breakable_scope(), {});
            return;
        case Unwind:
            if (!last_was_finally)
                emit<Bytecode::Op::LeaveUnwindContext>();
            last_was_finally = false;
            break;
        case LeaveLexicalEnvironment:
            emit<Bytecode::Op::LeaveLexicalEnvironment>();
            break;
        case Continue:
            break;
        case ReturnToFinally: {
            auto& block = make_block(DeprecatedString::formatted("{}.break", current_block().name()));
            emit<Op::ScheduleJump>(Label { block });
            switch_to_basic_block(block);
            last_was_finally = true;
            break;
        };
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generate_break(DeprecatedFlyString const& break_label)
{
    size_t current_boundary = m_boundaries.size();
    bool last_was_finally = false;
    for (auto const& breakable_scope : m_breakable_scopes.in_reverse()) {
        for (; current_boundary > 0; --current_boundary) {
            auto boundary = m_boundaries[current_boundary - 1];
            if (boundary == BlockBoundaryType::Unwind) {
                if (!last_was_finally)
                    emit<Bytecode::Op::LeaveUnwindContext>();
                last_was_finally = false;
            } else if (boundary == BlockBoundaryType::LeaveLexicalEnvironment) {
                emit<Bytecode::Op::LeaveLexicalEnvironment>();
            } else if (boundary == BlockBoundaryType::ReturnToFinally) {
                auto& block = make_block(DeprecatedString::formatted("{}.break", current_block().name()));
                emit<Op::ScheduleJump>(Label { block });
                switch_to_basic_block(block);
                last_was_finally = true;
            } else if (boundary == BlockBoundaryType::Break) {
                // Make sure we don't process this boundary twice if the current breakable scope doesn't contain the target label.
                --current_boundary;
                break;
            }
        }

        if (breakable_scope.language_label_set.contains_slow(break_label)) {
            emit<Op::Jump>().set_targets(breakable_scope.bytecode_target, {});
            return;
        }
    }

    // We must have a breakable scope available that contains the label, as this should be enforced by the parser.
    VERIFY_NOT_REACHED();
}

void Generator::generate_continue()
{
    bool last_was_finally = false;
    // FIXME: Reduce code duplication
    for (size_t i = m_boundaries.size(); i > 0; --i) {
        auto boundary = m_boundaries[i - 1];
        using enum BlockBoundaryType;
        switch (boundary) {
        case Continue:
            emit<Op::Jump>().set_targets(nearest_continuable_scope(), {});
            return;
        case Unwind:
            if (!last_was_finally)
                emit<Bytecode::Op::LeaveUnwindContext>();
            last_was_finally = false;
            break;
        case LeaveLexicalEnvironment:
            emit<Bytecode::Op::LeaveLexicalEnvironment>();
            break;
        case Break:
            break;
        case ReturnToFinally: {
            auto& block = make_block(DeprecatedString::formatted("{}.continue", current_block().name()));
            emit<Op::ScheduleJump>(Label { block });
            switch_to_basic_block(block);
            last_was_finally = true;
            break;
        };
        }
    }
    VERIFY_NOT_REACHED();
}

void Generator::generate_continue(DeprecatedFlyString const& continue_label)
{
    size_t current_boundary = m_boundaries.size();
    bool last_was_finally = false;
    for (auto const& continuable_scope : m_continuable_scopes.in_reverse()) {
        for (; current_boundary > 0; --current_boundary) {
            auto boundary = m_boundaries[current_boundary - 1];
            if (boundary == BlockBoundaryType::Unwind) {
                if (!last_was_finally)
                    emit<Bytecode::Op::LeaveUnwindContext>();
                last_was_finally = false;
            } else if (boundary == BlockBoundaryType::LeaveLexicalEnvironment) {
                emit<Bytecode::Op::LeaveLexicalEnvironment>();
            } else if (boundary == BlockBoundaryType::ReturnToFinally) {
                auto& block = make_block(DeprecatedString::formatted("{}.continue", current_block().name()));
                emit<Op::ScheduleJump>(Label { block });
                switch_to_basic_block(block);
                last_was_finally = true;
            } else if (boundary == BlockBoundaryType::Continue) {
                // Make sure we don't process this boundary twice if the current continuable scope doesn't contain the target label.
                --current_boundary;
                break;
            }
        }

        if (continuable_scope.language_label_set.contains_slow(continue_label)) {
            emit<Op::Jump>().set_targets(continuable_scope.bytecode_target, {});
            return;
        }
    }

    // We must have a continuable scope available that contains the label, as this should be enforced by the parser.
    VERIFY_NOT_REACHED();
}

void Generator::push_home_object(Register register_)
{
    m_home_objects.append(register_);
}

void Generator::pop_home_object()
{
    m_home_objects.take_last();
}

void Generator::emit_new_function(FunctionNode const& function_node)
{
    if (m_home_objects.is_empty())
        emit<Op::NewFunction>(function_node);
    else
        emit<Op::NewFunction>(function_node, m_home_objects.last());
}

}
