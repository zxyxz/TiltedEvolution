// Shader Fixes
VersionDbPtr<uint8_t> BSBatchRenderer_SetupAndDrawPass(107644);
VersionDbPtr<uint8_t> BSLightingShader_SetupGeometry_ParallaxTechniqueLoc(107300);


// https://github.com/aers/EngineFixesSkyrim64/blob/e2510bcd79d57e9cc52e57417ca139dc699477b1/src/fixes/shaderfixes.cpp#L59
void PatchBSLightingShaderForceAlphaTest()
{
    struct BSBatchRenderer_SetupAndDrawPass_Code : TiltedPhoques::CodeGenerator
    {
        BSBatchRenderer_SetupAndDrawPass_Code(uint8_t* apLoc)
        {
            // 142F580
            mov(rax, rsp);
            push(rdi);
            push(r12);
            // 142F586

            // exit
            jmp(ptr[rip]);
            dq((uint64_t)apLoc + 0x6);
        }
    } gen(BSBatchRenderer_SetupAndDrawPass.Get());
    TiltedPhoques::Jump(BSBatchRenderer_SetupAndDrawPass.Get(), gen.getCode());

    spdlog::info("BSLightingShaderForceAlphaTest patched.");
}


// https://github.com/aers/EngineFixesSkyrim64/blob/e2510bcd79d57e9cc52e57417ca139dc699477b1/src/fixes/shaderfixes.cpp#L94C10-L94C52
void PatchBSLightingShaderSetupGeometryParallax()
{
    struct BSLightingShader_SetupGeometry_Parallax_Code : TiltedPhoques::CodeGenerator
    {
        BSLightingShader_SetupGeometry_Parallax_Code(uint8_t* apLoc)
        {
            // orig code
            test(eax, 0x21C00);
            mov(r9d, 1);
            cmovnz(ecx, r9d);

            // new code
            cmp(dword[rbp + 0x1D0 - 0x210], 0x3); // technique ID = PARALLAX
            cmovz(ecx, r9d);                      // set eye update true

            // jmp out
            jmp(ptr[rip]);
            dq((uint64_t)apLoc + 0xF);
        }
    } gen(BSLightingShader_SetupGeometry_ParallaxTechniqueLoc.Get() + 0xB5D);
    TiltedPhoques::Jump(BSLightingShader_SetupGeometry_ParallaxTechniqueLoc.Get() + 0xB5D, gen.getCode());

    spdlog::info("BSLightingShaderSetupGeometryParallax patched.");
}


static TiltedPhoques::Initializer s_shaderFixes([]() {
    PatchBSLightingShaderForceAlphaTest();
    PatchBSLightingShaderSetupGeometryParallax();
});
