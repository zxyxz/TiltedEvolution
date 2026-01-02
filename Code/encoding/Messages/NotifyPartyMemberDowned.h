#pragma once

#include "Message.h"
#include <Structs/GameId.h>

/**
 * Server -> Client notification sent to every party member when a party member becomes
 * downed (bleedout/unrecoverable) or is revived.
 *
 * Carries enough information for clients to:
 * - Identify the player (server-side player id)
 * - Know whether they are currently downed
 * - Optionally render UI/chat feedback about revival (e.g., via Healing Hands)
 * - Optionally place markers or effects using the position/worldspace/cell
 */
struct NotifyPartyMemberDowned final : ServerMessage
{
    static constexpr ServerOpcode Opcode = kNotifyPartyMemberDowned;

    NotifyPartyMemberDowned()
        : ServerMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifyPartyMemberDowned& acRhs) const noexcept
    {
        return PlayerId == acRhs.PlayerId &&
               ServerId == acRhs.ServerId &&
               IsDowned == acRhs.IsDowned &&
               PositionX == acRhs.PositionX &&
               PositionY == acRhs.PositionY &&
               PositionZ == acRhs.PositionZ &&
               WorldSpaceId == acRhs.WorldSpaceId &&
               CellId == acRhs.CellId &&
               GetOpcode() == acRhs.GetOpcode();
    }

    // Numeric identifier of the party member's actor on the server (entt id)
    uint32_t ServerId{0};

    // Server-side player id of the downed/revived party member
    uint32_t PlayerId{0};

    // True if the player is downed (in bleedout/unrecoverable), false if they have been revived
    bool IsDowned{false};

    // World position of the player at the time of the event (can be used for markers/effects)
    float PositionX{0.f};
    float PositionY{0.f};
    float PositionZ{0.f};

    // World identification for context (same shape used elsewhere in party/location messages)
    GameId WorldSpaceId{};
    GameId CellId{};
};
