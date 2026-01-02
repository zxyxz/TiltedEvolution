#pragma once

#include <errno.h>

struct BSSaveDataSystemUtility
{
    virtual ~BSSaveDataSystemUtility() = default;

    virtual bool CreateSaveDirectory(const char* a_pathName, bool a_ignoreINI) = 0;
    virtual errno_t PrepareFileSavePath(const char* a_fileName, char* a_dst, bool a_tmpSave, bool a_ignoreINI) = 0;
    virtual void Unk_03(void) = 0;
    virtual void Unk_04(void) = 0;
    virtual void Unk_05(void) = 0;
    virtual void Unk_06(void) = 0;
    virtual void Unk_07(void) = 0;
    virtual void Unk_08(void) = 0;
    virtual void Unk_09(void) = 0;
    virtual void Unk_0A(void) = 0;
    virtual void Unk_0B(void) = 0;
    virtual void Unk_0C(void) = 0;
    virtual void Unk_0D(void) = 0;
    virtual void Unk_0E(void) = 0;
    virtual void Unk_0F(void) = 0;
    virtual void Unk_10(void) = 0;
    virtual void Unk_11(void) = 0;
};

struct BSWin32SaveDataSystemUtility : BSSaveDataSystemUtility
{
    static BSWin32SaveDataSystemUtility* GetSingleton() noexcept;
};
