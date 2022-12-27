/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Kernel/Memory/AddressSpace.h>
#include <Kernel/Memory/MemoryManager.h>
#include <Kernel/Memory/ScopedAddressSpaceSwitcher.h>
#include <Kernel/Process.h>

namespace Kernel {

ScopedAddressSpaceSwitcher::ScopedAddressSpaceSwitcher(Process& process)
    : m_previous_address_space(*Thread::current()->user_address_space())
{
    auto address_space = process.address_space().with([&](auto& address_space) -> NonnullRefPtr<Memory::AddressSpace> { return *address_space; });
    VERIFY(Thread::current() != nullptr);
#if ARCH(X86_64)
    m_previous_cr3 = read_cr3();
#elif ARCH(AARC64)
    TODO_AARCH64();
#endif

    Memory::MemoryManager::enter_address_space(move(address_space));
}

ScopedAddressSpaceSwitcher::~ScopedAddressSpaceSwitcher()
{
    Memory::MemoryManager::enter_address_space(*m_previous_address_space);
}

}
