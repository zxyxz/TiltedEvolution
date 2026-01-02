#pragma once

#include <cstdint>

namespace DropExecution
{
enum class Mode
{
    None,
    LocalDrop,
    RemoteDrop,
    RemotePickup,
    LocalPickup
};

Mode GetCurrentMode() noexcept;
uint32_t GetCurrentActor() noexcept;
uint64_t GetCurrentDrop() noexcept;

struct Scope
{
    Scope(Mode aMode, uint32_t aActorFormId, uint64_t aDropId) noexcept;
    ~Scope();

private:
    Mode m_previousMode{Mode::None};
    uint32_t m_previousActor{0};
    uint64_t m_previousDrop{0};
};
} // namespace DropExecution
