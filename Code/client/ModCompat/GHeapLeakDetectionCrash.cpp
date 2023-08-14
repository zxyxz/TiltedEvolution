// GHeapLeakDetectionCrash
VersionDbPtr<uint8_t> GHeapLeakDetectionCrash_FuncBase(87837);

class GHeapLeakDetectionCrashPatch
{
  public:
    static void Install()
    {
        constexpr std::size_t START = 0x4B;
        constexpr std::size_t END = 0x5C;
        constexpr std::uint8_t NOP = 0x90;

        for (std::size_t i = START; i < END; ++i)
            TiltedPhoques::Nop(GHeapLeakDetectionCrash_FuncBase.Get() + i, 1);
    }
};


static TiltedPhoques::Initializer s_GHeapLeakDetectionCrash([]() {
    GHeapLeakDetectionCrashPatch::Install();

    spdlog::info("GHeapLeakDetectionCrash patched");
});
