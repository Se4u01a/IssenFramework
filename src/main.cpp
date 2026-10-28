#include "game/EnemyRules.h"
#include "game/Executor.h"
#include "game/Hooks.h"
#include "game/Issen.h"
#include "game/Menu.h"
#include "game/Settings.h"

namespace
{
	void SetupLog()
	{
		auto path = logger::log_directory();
		if (!path) {
			return;
		}
		*path /= "IssenFramework.log"sv;

		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);
	}

	void OnMessage(SKSE::MessagingInterface::Message* message)
	{
		switch (message->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			issen::game::enemy::Load();
			issen::game::executor::Init();
			issen::game::hooks::InstallInput();
			issen::game::hooks::InstallPerfectDodge();
			issen::game::menu::Install();
			break;
		case SKSE::MessagingInterface::kSaveGame:
			issen::game::executor::RestoreSpeed();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			issen::game::hooks::InstallHits();  // once, after every plugin's kDataLoaded hooks
			issen::game::executor::BindDismembering();
			issen::game::Reset();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SetupLog();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("{} v{} is loading...", plugin->GetName(), plugin->GetVersion());

	SKSE::Init(a_skse, { .log = false, .trampoline = true, .trampolineSize = 64 });

	issen::game::Settings::Load();
	issen::game::Configure();
	issen::game::hooks::InstallUpdate();

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);

	logger::info("{} has finished loading.", plugin->GetName());
	return true;
}
