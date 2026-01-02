#pragma once

#include "Message.h"
#include <Structs/GameId.h>

struct HealingProximityRequest final : ClientMessage
{
    static constexpr ClientOpcode Opcode = kRequestHealingProximity;

    HealingProximityRequest()
        : ClientMessage(Opcode)
    {
    }

    void SerializeRaw(TiltedPhoques::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept override;

    bool operator==(const HealingProximityRequest& acRhs) const noexcept 
    { 
        return CasterId == acRhs.CasterId && 
               CasterX == acRhs.CasterX && 
               CasterY == acRhs.CasterY && 
               CasterZ == acRhs.CasterZ && 
               SpellFormId == acRhs.SpellFormId && 
               CasterRestorationLevel == acRhs.CasterRestorationLevel &&
               GetOpcode() == acRhs.GetOpcode(); 
    }

    uint32_t CasterId;
    float CasterX;
    float CasterY;
    float CasterZ;
    GameId SpellFormId{};
    uint16_t CasterRestorationLevel = 0;
};
