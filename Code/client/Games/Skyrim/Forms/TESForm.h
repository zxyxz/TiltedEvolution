#pragma once

#include <Components/BaseFormComponent.h>
#include <Misc/BSFixedString.h>

enum class FormType : uint8_t
{
    Scroll = 0x17,
    Activator = 0x18,
    TalkingActivator = 0x19,
    Armor = 0x1A,
    Book = 0x1B,
    Container = 0x1C,
    Door = 0x1D,
    Ingredient = 0x1E,
    Light = 0x1F,
    Misc = 0x20,
    Apparatus = 0x21,
    Static = 0x22,
    StaticCollection = 0x23,
    MovableStatic = 0x24,
    Weapon = 0x29,
    Ammo = 0x2A,
    Npc = 0x2B,
    LeveledNPC = 0x2C,
    LeveledCharacter = LeveledNPC,
    KeyMaster = 0x2D,
    AlchemyItem = 0x2E,
    Alchemy = AlchemyItem,
    Note = 0x30,
    SoulGem = 0x34,
    LeveledItem = 0x35,
    ActorCharacter = 0x3E,
    Character = ActorCharacter,
    Quest = 0x4D,
    QuestItem = Quest,
    Count = 0x8A
};

struct BGSSaveFormBuffer;
struct BGSLoadFormBuffer;

struct TESForm : BaseFormComponent
{
    struct ChangeFlags
    {
        uint32_t flags{};
        uint64_t unk4{};
        uint32_t unkC{};
    };

    enum FormFlags
    {
        DISABLED = 1 << 0xB,
        IGNORE_FRIENDLY_HITS = 1 << 0x14,
    };

    static TESForm* GetById(uint32_t aId);

    virtual void sub_4();
    virtual void sub_5();
    virtual void sub_6();
    virtual void sub_7();
    virtual void sub_8();
    virtual void sub_9();
    virtual bool MarkChanged(uint32_t aChangeFlag);
    virtual bool UnsetChanged(uint32_t aChangeFlag);
    virtual void sub_C();
    virtual void sub_D();
    virtual void Save(BGSSaveFormBuffer* apBuffer) const noexcept;
    virtual void Load(BGSLoadFormBuffer* apBuffer);
    virtual void sub_10();
    virtual void sub_11();
    virtual void sub_12();
    virtual void InitializeComponents(); // Called after Load
    virtual void sub_14();
    virtual FormType GetFormType();
    virtual void sub_16();
    virtual void sub_17();
    virtual void sub_18();
    virtual void sub_19();
    virtual void sub_1A();
    virtual void sub_1B();
    virtual void sub_1C();
    virtual void sub_1D();
    virtual void sub_1E();
    virtual void sub_1F();
    virtual void sub_20();
    virtual void sub_21();
    virtual void sub_22();
    virtual void sub_23();
    virtual void sub_24();
    virtual void sub_25();
    virtual void sub_26();
    virtual void sub_27();
    virtual void sub_28();
    virtual void sub_29();
    virtual void sub_2A();
    virtual void sub_2B();
    virtual void sub_2C();
    virtual void sub_2D();
    // Actual function name is `GetTextForParsedSubTag`; passing an empty tag gets the full name of this form
    virtual const char* GetName(const BSFixedString& acTag = {}) const noexcept;
    virtual void CopyFrom(TESForm* apForm);
    virtual void sub_30();
    virtual void sub_31();
    virtual const char* GetFormEditorID();
    virtual void sub_33();
    virtual void sub_34();
    virtual void sub_35();
    virtual void sub_36();
    virtual void ActivateReference();
    virtual void sub_38();
    virtual void unk_39();
    virtual void unk_3A();

    // void CopyFromEx(TESForm* rhs);
    void Save_Reversed(uint32_t aChangeFlags, Buffer::Writer& aWriter);
    void SetSkipSaveFlag(bool aSet) noexcept;
    uint32_t GetChangeFlags() const noexcept;

    bool GetIgnoreFriendlyHit() const noexcept { return (flags & IGNORE_FRIENDLY_HITS) != 0; }
    void SetIgnoreFriendlyHit(bool aSet) noexcept
    {
        if (aSet)
            flags |= IGNORE_FRIENDLY_HITS;
        else
            flags &= ~IGNORE_FRIENDLY_HITS;
    }

    bool IsDisabled() const noexcept { return (flags & DISABLED) != 0; }
    bool IsTemporary() const noexcept { return formID >= 0xFF000000; }
    bool IsConsumable() const noexcept { return formType == FormType::Ingredient || formType == FormType::Alchemy; }

    uintptr_t unk4;
    uint32_t flags;
    uint32_t formID;
    uint16_t unk10;
    FormType formType;
    uint8_t padForm;
};

static_assert(sizeof(TESForm) == 0x20);
