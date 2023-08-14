// AutoScrapBuffer
VersionDbPtr<uint8_t> AutoScrapBuffer_Ctor(68108);
VersionDbPtr<uint8_t> AutoScrapBuffer_Dtor(68109);

// Memory Manager
VersionDbPtr<uint8_t> MemoryManager_GetSingleton(11141);

VersionDbPtr<uint8_t> MemoryManager_Allocate(68115);
VersionDbPtr<uint8_t> MemoryManager_ReAllocate(68116);
VersionDbPtr<uint8_t> MemoryManager_DeAllocate(68117);

VersionDbPtr<uint8_t> MemoryManager_Init(68121);



namespace AutoScrapBuffer
{
    void Ctor()
    {
        constexpr std::size_t size = 0x32 - 0x1D;
        TiltedPhoques::Fill(AutoScrapBuffer_Ctor.Get() + 0x1D, size, 0x90);
    }

    void Dtor()
    {
        {
            const auto dst = AutoScrapBuffer_Dtor.Get() + 0x12;

            struct Patch : TiltedPhoques::CodeGenerator
            {
                Patch(uint8_t* apLoc)
                {
                    xor_(rax, rax);
                    cmp(rbx, rax);
                }
            } gen(dst);
            TiltedPhoques::Jump(dst, gen.getCode());
            
            constexpr std::size_t size = 0x2F - 0x12;
            TiltedPhoques::Fill(dst, size, 0x90);
        }

        {
            const auto dst = AutoScrapBuffer_Dtor.Get() + 0x2F;
            TiltedPhoques::Put(dst, std::uint8_t{0x74}); // jnz -> jz
        }
    }

    void Install()
    {
        Ctor();
        Dtor();
    }
}
/*
namespace MemoryManager
{
    /*void* Allocate(std::size_t a_size, std::uint32_t a_alignment, bool a_alignmentRequired)
    {
        if (a_size > 0)
            return a_alignmentRequired ? scalable_aligned_malloc(a_size, a_alignment) : scalable_malloc(a_size);
        else
            return g_trash;
    }

    void Deallocate(void* a_mem, bool a_alignmentRequired)
    {
        if (a_mem != g_trash)
            a_alignmentRequired ? scalable_aligned_free(a_mem) : scalable_free(a_mem);
    }

    /*void* Reallocate(RE::MemoryManager* a_self, void* a_oldMem, std::size_t a_newSize, std::uint32_t a_alignment,
                     bool a_alignmentRequired)
    {
        if (a_oldMem == g_trash)
            return Allocate(a_self, a_newSize, a_alignment, a_alignmentRequired);
        else
            return a_alignmentRequired ? scalable_aligned_realloc(a_oldMem, a_newSize, a_alignment)
                                        : scalable_realloc(a_oldMem, a_newSize);
    }

    void ReplaceAllocRoutines()
    {
        TiltedPhoques::PutCall(MemoryManager_Allocate.Get(), &Allocate);
        TiltedPhoques::PutCall(MemoryManager_DeAllocate.Get(), &Deallocate);
        //TiltedPhoques::PutCall(MemoryManager_ReAllocate.Get(), &Reallocate);

        /*
        using tuple_t = std::tuple<REL::ID, std::size_t, void*>;
        const std::array todo{
            tuple_t{offsets::MemoryManager::MemoryManager_Allocate, 0x248, &Allocate},
            tuple_t{offsets::MemoryManager::MemoryManager_DeAllocate, 0x114, &Deallocate},
            tuple_t{offsets::MemoryManager::MemoryManager_ReAllocate, 0x1F6, &Reallocate},
        };

        for (const auto& [offset, size, func] : todo)
        {
            REL::Relocation<std::uintptr_t> target{offset};
            REL::safe_fill(target.address(), REL::INT3, size);
            asm_jump(target.address(), size, reinterpret_cast<std::uintptr_t>(func));
        }
        
    }

    void StubInit()
    {
        TiltedPhoques::Fill(MemoryManager_Init.Get(), 0xCC, 423);
        TiltedPhoques::Put(MemoryManager_Init.Get(), 0xC3);
    }

    void Install()
    {
        StubInit();
        ReplaceAllocRoutines();

        //TiltedPhoques::ThisCall(MemoryManager_GetSingleton.Get(), );
        
        //RE::MemoryManager::GetSingleton()->RegisterMemoryManager();
        //RE::BSThreadEvent::InitSDM();
    }
}


namespace ScrapHeap
{
    void* Allocate(RE::ScrapHeap*, std::size_t a_size, std::size_t a_alignment)
    {
            return a_size > 0 ? scalable_aligned_malloc(a_size, a_alignment) : g_trash;
    }

    RE::ScrapHeap* Ctor(RE::ScrapHeap* a_this)
    {
            std::memset(a_this, 0, sizeof(RE::ScrapHeap));
            reinterpret_cast<std::uintptr_t*>(a_this)[0] = offsets::MemoryManager::ScrapHeap_vtbl.address();
            return a_this;
    }

    void Deallocate(RE::ScrapHeap*, void* a_mem)
    {
            if (a_mem != g_trash)
                scalable_aligned_free(a_mem);
    }

    void WriteHooks()
    {
            using tuple_t = std::tuple<REL::ID, std::size_t, void*>;
            const std::array todo{
                tuple_t{offsets::MemoryManager::ScrapHeap_Allocate, 0x5E7, &Allocate},
                tuple_t{offsets::MemoryManager::ScrapHeap_DeAllocate, 0x13E, &Deallocate},
                tuple_t{offsets::MemoryManager::ScrapHeap_ctor, 0x13A, &Ctor},
            };

            for (const auto& [offset, size, func] : todo)
            {
                REL::Relocation<std::uintptr_t> target{offset};
                REL::safe_fill(target.address(), REL::INT3, size);
                asm_jump(target.address(), size, reinterpret_cast<std::uintptr_t>(func));
            }
    }

    void WriteStubs()
    {
            using tuple_t = std::tuple<REL::ID, std::size_t>;
            const std::array todo{
                tuple_t{offsets::MemoryManager::ScrapHeap_Clean, 0xBA},            // Clean
                tuple_t{offsets::MemoryManager::ScrapHeap_ClearKeepPages, 0x8},    // ClearKeepPages
                tuple_t{offsets::MemoryManager::ScrapHeap_InsertFreeBlock, 0xF6},  // InsertFreeBlock
                tuple_t{offsets::MemoryManager::ScrapHeap_RemoveFreeBlock, 0x185}, // RemoveFreeBlock
                tuple_t{offsets::MemoryManager::ScrapHeap_SetKeepPages, 0x4},      // SetKeepPages
                tuple_t{offsets::MemoryManager::ScrapHeap_Dtor, 0x32},             // dtor
            };

            for (const auto& [offset, size] : todo)
            {
                REL::Relocation<std::uintptr_t> target{offset};
                REL::safe_fill(target.address(), REL::INT3, size);
                REL::safe_write(target.address(), REL::RET);
            }
    }

    void Install()
    {
            WriteStubs();
            WriteHooks();
    }
}*/


static TiltedPhoques::Initializer s_memoryManager([]() {
    AutoScrapBuffer::Install();
    //MemoryManager::Install();
    //ScrapHeap::Install();

    spdlog::info("Memory Manager patched.");
    // const VersionDbPtr<void> MemoryManager_Init(68121);
    // TiltedPhoques::Fill(mem::pointer(MemoryManager_Init.Get()), 1, 0xCC);
});
