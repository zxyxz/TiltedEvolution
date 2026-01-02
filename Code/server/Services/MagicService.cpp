#include <Services/MagicService.h>

#include <GameServer.h>
#include <World.h>

#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/HealingProximityRequest.h>

#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifyHealingProximity.h>

#include <Components.h>

MagicService::MagicService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_spellCastConnection = aDispatcher.sink<PacketEvent<SpellCastRequest>>().connect<&MagicService::OnSpellCastRequest>(this);
    m_interruptCastConnection = aDispatcher.sink<PacketEvent<InterruptCastRequest>>().connect<&MagicService::OnInterruptCastRequest>(this);
    m_addTargetConnection = aDispatcher.sink<PacketEvent<AddTargetRequest>>().connect<&MagicService::OnAddTargetRequest>(this);
    m_removeSpellConnection = aDispatcher.sink<PacketEvent<RemoveSpellRequest>>().connect<&MagicService::OnRemoveSpellRequest>(this);
    m_healingProximityConnection = aDispatcher.sink<PacketEvent<HealingProximityRequest>>().connect<&MagicService::OnHealingProximityRequest>(this);
}

void MagicService::OnSpellCastRequest(const PacketEvent<SpellCastRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifySpellCast notify;
    notify.CasterId = message.CasterId;
    notify.SpellFormId = message.SpellFormId;
    notify.CastingSource = message.CastingSource;
    notify.IsDualCasting = message.IsDualCasting;
    notify.DesiredTarget = message.DesiredTarget;

    const auto entity = m_world.TryResolveEntity(message.CasterId);
    if (!entity)
    {
        spdlog::debug("Spell cast request for unknown caster {:X}", message.CasterId);
        return;
    }

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnInterruptCastRequest(const PacketEvent<InterruptCastRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifyInterruptCast notify;
    notify.CasterId = message.CasterId;
    notify.CastingSource = message.CastingSource;

    const auto entity = m_world.TryResolveEntity(message.CasterId);
    if (!entity)
    {
        spdlog::debug("Interrupt cast request for unknown caster {:X}", message.CasterId);
        return;
    }

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnAddTargetRequest(const PacketEvent<AddTargetRequest>& acMessage) const noexcept
{
    auto& message = acMessage.Packet;

    NotifyAddTarget notify;
    notify.TargetId = message.TargetId;
    notify.CasterId = message.CasterId;
    notify.SpellId = message.SpellId;
    notify.EffectId = message.EffectId;
    notify.Magnitude = message.Magnitude;
    notify.IsDualCasting = message.IsDualCasting;
    notify.ApplyHealPerkBonus = message.ApplyHealPerkBonus;
    notify.ApplyStaminaPerkBonus = message.ApplyStaminaPerkBonus;

    const auto entity = m_world.TryResolveEntity(message.TargetId);
    if (!entity)
    {
        spdlog::debug("Add target request for unknown target {:X}", message.TargetId);
        return;
    }

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnRemoveSpellRequest(const PacketEvent<RemoveSpellRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    
    NotifyRemoveSpell notify;
    notify.TargetId = message.TargetId;
    notify.SpellId = message.SpellId;

    //spdlog::info(__FUNCTION__ ": TargetId: {}, Spell baseId: {}", notify.TargetId, notify.SpellId.BaseId);

    const auto entity = m_world.TryResolveEntity(message.TargetId);
    if (!entity)
    {
        spdlog::debug("Remove spell request for unknown target {:X}", message.TargetId);
        return;
    }

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
}

void MagicService::OnHealingProximityRequest(const PacketEvent<HealingProximityRequest>& acMessage) const noexcept
{
    const auto& message = acMessage.Packet;
    
    NotifyHealingProximity notify;
    notify.CasterId = message.CasterId;
    notify.CasterX = message.CasterX;
    notify.CasterY = message.CasterY;
    notify.CasterZ = message.CasterZ;
    notify.SpellFormId = message.SpellFormId;
    notify.CasterRestorationLevel = message.CasterRestorationLevel;

    const auto entity = m_world.TryResolveEntity(message.CasterId);
    if (!entity)
    {
        spdlog::debug("Healing proximity request for unknown caster {:X}", message.CasterId);
        return;
    }

    if (!GameServer::Get()->SendToPlayersInRange(notify, *entity, acMessage.GetSender()))
        spdlog::error("{}: SendToPlayersInRange failed for healing proximity", __FUNCTION__);

    if (auto* pCaster = acMessage.GetSender())
        pCaster->Send(notify);
}
