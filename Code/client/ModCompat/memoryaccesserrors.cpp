//#include <Games/Skyrim/RTTI.h>
//#include <components/es_loader/TESFile.h>

VersionDbPtr<uint8_t> BSLightingShaderMaterialSnow_vtbl(254759);
VersionDbPtr<uint8_t> BSLightingShader_SetupMaterial(107298); // BSLightingShader::vf4

VersionDbPtr<uint8_t> BGSShaderParticleGeometryData_vtbl(189948);

VersionDbPtr<uint8_t> BSShadowDirectionalLight_vf16(108496);

class MemoryAccessErrorsPatch
{
    public:
        static void Install()
        {
            //PatchSnowMaterialCase();
            //PatchShaderParticleGeometryDataLimit();
            PatchUseAfterFree();
        }

    private:
        //static void PatchSnowMaterialCase()
        //{
        //    //logger::trace("patching BSLightingShader::SetupMaterial snow material case"sv);

        //    struct Patch : TiltedPhoques::CodeGenerator
        //    {
        //        Patch(uint8_t* a_vtbl, uint8_t* a_hook, uint8_t* a_exit)
        //        {
        //            Xbyak::Label vtblAddr;
        //            Xbyak::Label snowRetnLabel;
        //            Xbyak::Label exitRetnLabel;

        //            mov(rax, ptr[rip + vtblAddr]);
        //            cmp(rax, qword[rbx]);
        //            je("IS_SNOW");

        //            // not snow, fill with 0 to disable effect
        //            mov(eax, 0x00000000);
        //            mov(dword[rcx + rdx * 4 + 0xC], eax);
        //            mov(dword[rcx + rdx * 4 + 0x8], eax);
        //            mov(dword[rcx + rdx * 4 + 0x4], eax);
        //            mov(dword[rcx + rdx * 4], eax);
        //            jmp(ptr[rip + exitRetnLabel]);

        //            // is snow, jump out to original except our overwritten instruction
        //            L("IS_SNOW");
        //            movss(xmm2, dword[rbx + 0xAC]);
        //            jmp(ptr[rip + snowRetnLabel]);

        //            L(vtblAddr);
        //            dq((uint64_t)a_vtbl);

        //            L(snowRetnLabel);
        //            dq((uint64_t)a_hook + 0x8);

        //            L(exitRetnLabel);
        //            dq((uint64_t)a_exit);
        //        }
        //    } gen(BSLightingShaderMaterialSnow_vtbl.Get(), BSLightingShader_SetupMaterial.Get() + 0x6A6, BSLightingShader_SetupMaterial.Get() + 0x77D);

        //    TiltedPhoques::Put(BSLightingShaderMaterialSnow_vtbl.Get(), gen.getCode());
        //}

    //static bool (*Load)(BGSShaderParticleGeometryData* a_this, ESLoader::TESFile* a_file);

    //static void PatchShaderParticleGeometryDataLimit()
    //{

    //    // REL::Relocation<std::uintptr_t> vtbl{offsets::MemoryAccessErrors::BGSShaderParticleGeometryData_vtbl};
    //    //_Load = vtbl.write_vfunc(0x6, Load);

    //    TiltedPhoques::SwapCall(BGSShaderParticleGeometryData_vtbl.Get() + 0x06, Load, &LoadHook);
    //}

        static void PatchUseAfterFree()
        {

            struct Patch : Xbyak::CodeGenerator
            {
                Patch()
                {
                    mov(r9, r15);
                    nop();
                    nop();
                    nop();
                    nop();
                }
            };

            Patch patch;
            patch.ready();

            TiltedPhoques::Put(BSShadowDirectionalLight_vf16.Get() + 0x1BED, patch.getCode());
        }

    // BGSShaderParticleGeometryData::Load
    //static bool LoadHook(BGSShaderParticleGeometryData* a_this, ESLoader::TESFile* a_file)
    //{
    //    const bool retVal = TiltedPhoques::ThisCall(BGSShaderParticleGeometryData_vtbl.Get() + 0x06, a_this, a_file);

    //    // the game doesn't allow more than 10 here
    //    /*if (a_this->data.size() >= 12)
    //    {
    //        const auto particleDensity = a_this->data[11];
    //        if (particleDensity.f > 10.0)
    //            a_this->data[11].f = 10.0f;
    //    }*/

    //    return retVal;

    //    // inline static REL::Relocation<decltype(&RE::BGSShaderParticleGeometryData::Load)> _Load;
    //}
};

static TiltedPhoques::Initializer s_MemoryAccessErrors([]() {
    MemoryAccessErrorsPatch::Install();

    spdlog::info("MemoryAccessErrors patched.");
});
