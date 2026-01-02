#include "DropExecutionContext.h"

namespace DropExecution
{
namespace
{
    thread_local Mode g_mode = Mode::None;
    thread_local uint32_t g_actorFormId = 0;
    thread_local uint64_t g_dropId = 0;
}

Mode GetCurrentMode() noexcept
{
    return g_mode;
}

uint32_t GetCurrentActor() noexcept
{
    return g_actorFormId;
}

uint64_t GetCurrentDrop() noexcept
{
    return g_dropId;
}

Scope::Scope(Mode aMode, uint32_t aActorFormId, uint64_t aDropId) noexcept
    : m_previousMode(g_mode)
    , m_previousActor(g_actorFormId)
    , m_previousDrop(g_dropId)
{
    g_mode = aMode;
    g_actorFormId = aActorFormId;
    g_dropId = aDropId;
}

Scope::~Scope()
{
    g_mode = m_previousMode;
    g_actorFormId = m_previousActor;
    g_dropId = m_previousDrop;
}
} // namespace DropExecution
