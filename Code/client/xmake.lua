
local function build_client(name)
target(name)
    set_kind("static")
    set_group("Client")
    add_includedirs(".","../../Libraries/")
    set_pcxxheader("TiltedOnlinePCH.h")

    -- exclude game specifc stuff
    add_headerfiles("**.h|Games/Skyrim/**|Services/Vivox/**")
    add_files("**.cpp|Games/Skyrim/**|Services/Vivox/**")

    after_build(function(target)
        local isolationdir = path.join(target:scriptdir(), "..", "..", "Isolation")
        if os.isdir(isolationdir) then
            local outdir = target:targetdir() or path.directory(target:targetfile())
            local dest = path.join(outdir, "Isolation")
            os.mkdir(outdir)
            os.tryrm(dest)
            -- Copy the folder itself into the output dir (avoids Isolation/Isolation nesting).
            os.cp(isolationdir, outdir)
        end
    end)

    after_install(function(target)
        -- copy dlls
        for _, pkg_with_dlls in ipairs({"cef", "discord"}) do
            local linkdir = target:pkg(pkg_with_dlls):get("linkdirs")
            local bindir = path.join(linkdir, "..", "bin")
            os.cp(bindir, target:installdir())
        end
        -- copy ui assets needed by overlay/branding
        local uidir = path.join(target:scriptdir(), "..", "skyrim_ui", "src")
        os.cp(path.join(uidir, "assets", "images", "cursor.dds"), path.join(target:installdir(), "bin", "assets", "images", "cursor.dds"))
        os.cp(path.join(uidir, "assets", "images", "cursor.png"), path.join(target:installdir(), "bin", "assets", "images", "cursor.png"))
        -- font used for Skyrim-like branding
        os.cp(path.join(uidir, "assets", "fonts", "futura-condensed", "futura-condensed-medium.otf"),
              path.join(target:installdir(), "bin", "assets", "fonts", "futura-condensed", "futura-condensed-medium.otf"))
        -- quest isolation data
        local isolationdir = path.join(target:scriptdir(), "..", "..", "Isolation")
        if os.isdir(isolationdir) then
            local bindir = path.join(target:installdir(), "bin")
            local dest = path.join(bindir, "Isolation")
            os.mkdir(bindir)
            os.tryrm(dest)
            -- Copy the folder itself into the bin dir (avoids Isolation/Isolation nesting).
            os.cp(isolationdir, bindir)
        end
        os.rm(path.join(target:installdir(), "bin", "**Tests.exe"))
    end)

    add_files("Games/Skyrim/**.cpp")
    add_headerfiles("Games/Skyrim/**.h")
    -- rather hacky:
    add_includedirs("Games/Skyrim")
    add_deps("SkyrimEncoding")
    add_deps(
        "UiProcess",
        "CommonLib",
        "BaseLib",
        "ImGuiImpl",
        "TiltedConnect",
        "TiltedReverse",
        "TiltedHooks",
        "TiltedUi",
        "ESLoader",
        {inherit = true}
    )

    add_packages(
        "tiltedcore",
        "spdlog",
        "fmt",
        "hopscotch-map",
        "cryptopp",
        "gamenetworkingsockets",
        "discord",
        "imgui",
        "cef",
        "minhook",
        "entt",
        "glm",
        "mem",
        "xbyak",
        "fmt")

    if has_config("vivox") then
        add_files("Services/Vivox/**.cpp")
        add_headerfiles("Services/Vivox/**.h")
        add_includedirs("Services/Vivox")
        add_deps("Vivox")
        add_defines("TP_VIVOX=1")
    else
        add_defines("TP_VIVOX=0")
    end

    add_syslinks(
        "version",
        "dbghelp",
        "kernel32")
end

add_requires("tiltedcore v0.2.7", {debug = true})

build_client("SkyrimTogetherClient")
