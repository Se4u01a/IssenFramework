#include "EnemyRules.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace issen::game::enemy
{
	namespace
	{
		constexpr auto          kRulesDir = "Data/SKSE/Plugins/IssenFramework/Rules";
		constexpr RE::FormID    kBossLocRefType = 0x000130F7;  // Skyrim.esm "Boss"

		rules::RuleSet g_rules;

		// The perks the rules name, resolved once (kDataLoaded).
		std::vector<std::pair<std::string, RE::BGSPerk*>> g_rulePerks;

		void AddKeywords(std::vector<std::string>& out, const RE::BGSKeywordForm* form)
		{
			if (!form) {
				return;
			}
			for (const auto* kw : form->GetKeywords()) {
				if (kw) {
					if (const char* id = kw->GetFormEditorID(); id && *id) {
						out.emplace_back(id);
					}
				}
			}
		}


		rules::Subject SubjectOf(RE::Actor* actor, rules::Kind kind)
		{
			rules::Subject s;
			s.kind = kind;
			auto* base = actor->GetActorBase();
			auto* race = actor->GetRace();
			AddKeywords(s.keywords, base);
			AddKeywords(s.keywords, race);
			if (race) {
				if (const char* id = race->GetFormEditorID(); id) {
					s.race = id;
				}
			}
			s.level = actor->GetLevel();
			s.boss = IsBoss(actor);
			s.unique = base && base->IsUnique();
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				for (const auto& [name, perk] : g_rulePerks) {
					if (perk && player->HasPerk(perk)) {
						s.playerPerks.push_back(name);
					}
				}
			}
			return s;
		}
	}

	bool IsBoss(RE::Actor* actor)
	{
		const auto* extra = actor ? actor->extraList.GetByType<RE::ExtraLocationRefType>() : nullptr;
		return extra && extra->locRefType && extra->locRefType->GetFormID() == kBossLocRefType;
	}

	void Load()
	{
		std::error_code ec;
		std::size_t     files = 0;
		for (const auto& entry : std::filesystem::directory_iterator(kRulesDir, ec)) {
			if (!entry.is_regular_file() || entry.path().extension() != ".json") {
				continue;
			}
			std::ifstream     in(entry.path(), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			const auto name = entry.path().filename().string();
			if (const auto error = g_rules.AddFile(ss.str(), name); !error.empty()) {
				logger::error("rules: {} skipped:\n{}", name, error);
				continue;
			}
			++files;
		}
		g_rules.Finish();
		logger::info("rules: {} rule(s) from {} file(s) in {}", g_rules.Rules().size(), files, kRulesDir);
		for (const auto& r : g_rules.Rules()) {
			logger::info("  [{}] {}{}", r.priority, r.name, r.enabled ? "" : " (disabled)");
		}
		if (g_rules.Rules().empty()) {
			logger::warn("rules: none loaded; no issen will fire");
		}
		g_rulePerks.clear();
		for (const auto& name : g_rules.PerkNames()) {
			auto* perk = LookupPerk(name);
			g_rulePerks.emplace_back(name, perk);
			if (perk) {
				logger::info("rules: player perk '{}' -> {:08X}", name, perk->GetFormID());
			} else {
				logger::warn("rules: player perk '{}' not found; conditions on it treat it as missing", name);
			}
		}
	}

	RE::BGSPerk* LookupPerk(const std::string& name)
	{
		if (name.empty()) {
			return nullptr;
		}
		if (const auto bar = name.find('|'); bar != std::string::npos) {
			auto*      data = RE::TESDataHandler::GetSingleton();
			const auto id = static_cast<RE::FormID>(std::strtoul(name.c_str() + bar + 1, nullptr, 0));
			return data ? data->LookupForm<RE::BGSPerk>(id, name.substr(0, bar)) : nullptr;
		}
		return RE::TESForm::LookupByEditorID<RE::BGSPerk>(name);
	}

	bool PlayerHasPerk(const std::string& name)
	{
		if (name.empty()) {
			return true;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* perk = LookupPerk(name);
		return player && perk && player->HasPerk(perk);
	}

	const rules::Rule* Match(RE::Actor* actor, rules::Kind kind)
	{
		return actor ? g_rules.Match(SubjectOf(actor, kind)) : nullptr;
	}
}
