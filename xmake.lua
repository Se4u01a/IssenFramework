set_xmakever("3.0.8")

includes("../CommonLibSSE-NG")

set_project("IssenFramework")
set_version("0.1.0")

add_rules("mode.debug", "mode.releasedbg")
set_defaultplat("windows")
set_defaultarchs("windows|x64")
set_allowedplats("windows")
set_allowedarchs("x64")
set_defaultmode("releasedbg")
set_languages("c++latest")

set_warnings("allextra")

set_policy("build.fence", true)
set_policy("check.auto_ignore_flags", false)

-- Skyrim SE 1.5.97 only, like Reactive Dodge Framework.
set_config("skyrim_vr", false)
set_config("skyrim_ae", false)
set_config("skyrim_se", true)
set_config("skse_xbyak", true)

-- Rule files (JSON): glaze reflects the rule structs and reports errors by return value.
add_requires("glaze 7.0.2")

local deploy_dir = "D:/Games/stb/[STB] Mod Organizer/mods/IssenFramework/SKSE/Plugins"

target("IssenFramework")
    add_deps("commonlibsse-ng")
    add_packages("glaze")
    add_rules("commonlibsse-ng.plugin", {
        name = "IssenFramework",
        author = "newrite",
        description = "Issen Framework: Onimusha-style counter kills for Skyrim SE 1.5.97"
    })

    add_includedirs("src")
    -- FLICK (FUCK.dll) API header and the ImGui headers of the revision it is built with (types only)
    add_includedirs("src/third_party/imgui", "src/third_party/fuck")
    set_encodings("utf-8")
    set_pcxxheader("src/game/pch.h")
    add_headerfiles("src/**.h")
    add_files("src/main.cpp", "src/game/**.cpp", "src/core/**.cpp")

    after_build(function (target)
        os.mkdir(deploy_dir)
        os.cp(target:targetfile(), deploy_dir)
        local pdb = path.join(path.directory(target:targetfile()), target:basename() .. ".pdb")
        if os.isfile(pdb) then
            os.cp(pdb, deploy_dir)
        end
        -- default rules (ours, always refreshed): SKSE/Plugins/IssenFramework/Rules/
        local rules = path.join(deploy_dir, "IssenFramework", "Rules")
        os.mkdir(rules)
        os.cp("dist/Rules/*.json", rules)
        local ini = path.join(deploy_dir, "IssenFramework.ini")
        if not os.isfile(ini) then
            os.cp("dist/IssenFramework.ini", ini)
        end
    end)

-- Engine-free core + unit tests: no CommonLib.
target("issen_tests")
    set_kind("binary")
    set_rundir("$(projectdir)")  -- tests read dist/ files by their repo path
    add_packages("glaze")
    add_includedirs("src", "tests")
    add_headerfiles("src/core/**.h", "tests/**.h")
    add_files("src/core/**.cpp", "tests/**.cpp")
