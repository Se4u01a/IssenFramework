set_xmakever("3.0.8")

-- CommonLibSSE-NG: the extern/CommonLibSSE-NG submodule (git clone --recursive), or a checkout of your own in
-- COMMONLIBSSE_NG_PATH.
includes(os.getenv("COMMONLIBSSE_NG_PATH") or "extern/CommonLibSSE-NG")

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

-- Optional: after each build copy the DLL, the default rules and (only if missing) the ini into a mod folder, e.g.
--   xmake f --deploy_dir="C:/MO2/mods/IssenFramework/SKSE/Plugins"
option("deploy_dir")
    set_default("")
    set_showmenu(true)
    set_description("SKSE/Plugins folder of the mod to copy the build into (empty = no copy)")
option_end()

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
        local deploy_dir = get_config("deploy_dir")
        if not deploy_dir or deploy_dir == "" then
            return
        end
        os.mkdir(deploy_dir)
        if not os.trycp(target:targetfile(), deploy_dir) then
            cprint("${yellow}warning: %s is in use (the game is running?): not copied", path.join(deploy_dir, path.filename(target:targetfile())))
            return
        end
        local pdb = path.join(path.directory(target:targetfile()), target:basename() .. ".pdb")
        if os.isfile(pdb) then
            os.trycp(pdb, deploy_dir)
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

-- xmake pack: build/IssenFramework-<version>.zip, the mod archive (SKSE/Plugins/...) for a mod manager.
task("pack")
    set_menu({ usage = "xmake pack", description = "Build the mod archive in build/", options = {} })
    on_run(function ()
        import("core.project.config")
        import("core.project.project")
        import("utils.archive")
        config.load()
        os.exec("xmake build -y IssenFramework")
        local dll = path.join(config.builddir(), config.plat(), config.arch(), config.mode(), "IssenFramework.dll")
        local stage = path.join("build", "pack")
        local plugins = path.join(stage, "SKSE", "Plugins")
        os.tryrm(stage)
        os.mkdir(path.join(plugins, "IssenFramework", "Rules"))
        os.cp(dll, plugins)
        os.cp("dist/IssenFramework.ini", plugins)
        os.cp("dist/Rules/*.json", path.join(plugins, "IssenFramework", "Rules"))
        local zip = path.absolute(path.join("build", "IssenFramework-" .. project.version() .. ".zip"))
        os.tryrm(zip)
        archive.archive(zip, "SKSE", { curdir = stage })
        cprint("${bright green}mod archive: %s", zip)
    end)
