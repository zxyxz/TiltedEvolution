Scriptname TO_PartyTracker extends Quest
{Party quest used to show objective markers on party members.}

Int Property ObjectiveBase Auto ; First objective index
Int Property AliasCount Auto     ; Number of party alias slots in this quest

Function SetEnabled(Bool enabled)
    if enabled
        Start()
        SetActive(True)
    else
        SetActive(False)
        Stop()
    EndIf
EndFunction

Function ClearAll()
    int i = 0
    while i < AliasCount
        Alias a = GetAlias(i)
        ReferenceAlias ra = a as ReferenceAlias
        if ra
            ra.Clear()
        endif
        SetObjectiveDisplayed(ObjectiveBase + i, False, True)
        i += 1
    EndWhile
EndFunction

Function SetPartyAlias(Int index, ObjectReference ref)
    if index < 0 || index >= AliasCount
        return
    endif

    Alias a = GetAlias(index)
    ReferenceAlias ra = a as ReferenceAlias
    if !ra
        return
    endif

    if ref
        ra.ForceRefTo(ref)
        SetObjectiveDisplayed(ObjectiveBase + index, True, False)
    else
        ra.Clear()
        SetObjectiveDisplayed(ObjectiveBase + index, False, True)
    endif
EndFunction

