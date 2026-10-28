#include "check.h"

#include "core/Rules.h"

#include <fstream>
#include <sstream>

using namespace issen::rules;

namespace
{
	Subject Npc(bool boss = false)
	{
		Subject s;
		s.keywords = { "ActorTypeNPC" };
		s.race = "NordRace";
		s.level = 10;
		s.boss = boss;
		return s;
	}

	RuleSet DefaultRules()
	{
		std::ifstream     in("dist/Rules/IssenDefault.json", std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		RuleSet set;
		const auto error = set.AddFile(ss.str(), "IssenDefault.json");
		CHECK(error.empty());
		if (!error.empty()) {
			std::printf("    %s\n", error.c_str());
		}
		set.Finish();
		return set;
	}
}

TEST(default_rules_parse)
{
	const auto set = DefaultRules();
	CHECK(set.Rules().size() == 8);
}

TEST(default_humanoid_is_killed)
{
	const auto  set = DefaultRules();
	const auto* rule = set.Match(Npc());
	CHECK(rule && rule->outcome.mode == Mode::Kill);
	CHECK(rule && rule->outcome.dismember.nodes.size() == 5);  // full pool: head, arms and legs
}

TEST(default_humanoid_after_a_perfect_dodge_loses_the_head)
{
	const auto set = DefaultRules();
	auto       s = Npc();
	s.kind = Kind::PerfectDodge;
	const auto* rule = set.Match(s);
	CHECK(rule && rule->outcome.mode == Mode::Kill);
	CHECK(rule && rule->outcome.dismember.nodes.size() == 5 && rule->outcome.dismember.nodes[0].node == "NPC Neck [Neck]");
}

TEST(default_boss_takes_damage)
{
	const auto  set = DefaultRules();
	const auto* rule = set.Match(Npc(true));
	CHECK(rule && rule->outcome.mode == Mode::Damage);
	CHECK(rule && rule->priority == 900);
}

TEST(default_dragon_is_immune)
{
	const auto set = DefaultRules();
	Subject    s;
	s.keywords = { "ActorTypeDragon", "ActorTypeCreature" };
	const auto* rule = set.Match(s);
	CHECK(rule && rule->outcome.mode == Mode::None);
}

TEST(default_creature_falls_through)
{
	const auto set = DefaultRules();
	Subject    s;
	s.keywords = { "ActorTypeAnimal", "ActorTypeCreature" };
	const auto* rule = set.Match(s);
	CHECK(rule && rule->priority == 0 && rule->outcome.mode == Mode::Damage);
}

TEST(keywords_compare_without_case)
{
	When w;
	w.keywordsAny = { "actortypenpc" };
	CHECK(Matches(w, Npc()));
}

TEST(keywords_all_and_none)
{
	When w;
	w.keywordsAll = { "ActorTypeNPC", "Vampire" };
	CHECK(!Matches(w, Npc()));
	auto s = Npc();
	s.keywords.push_back("Vampire");
	CHECK(Matches(w, s));
	w.keywordsNone = { "vampire" };
	CHECK(!Matches(w, s));
}

TEST(race_level_unique_kind)
{
	When w;
	w.racesAny = { "NordRace", "ImperialRace" };
	w.levelMin = 5;
	w.levelMax = 20;
	w.unique = false;
	w.kind = Kind::PerfectDodge;
	auto s = Npc();
	CHECK(!Matches(w, s));  // kind Normal
	s.kind = Kind::PerfectDodge;
	CHECK(Matches(w, s));
	s.level = 21;
	CHECK(!Matches(w, s));
	s.level = 10;
	s.unique = true;
	CHECK(!Matches(w, s));
}

TEST(priority_then_file_order)
{
	RuleSet set;
	CHECK(set.AddFile(R"({"rules":[{"name":"a","priority":1,"outcome":{"mode":"Damage"}},{"name":"b","priority":5},{"name":"c","priority":5}]})", "x").empty());
	set.Finish();
	CHECK(set.Match(Npc())->name == "b");
}

TEST(disabled_rule_is_skipped)
{
	RuleSet set;
	CHECK(set.AddFile(R"({"rules":[{"name":"off","enabled":false,"priority":9},{"name":"on"}]})", "x").empty());
	set.Finish();
	CHECK(set.Match(Npc())->name == "on");
}

TEST(unknown_key_is_an_error)
{
	RuleSet set;
	CHECK(!set.AddFile(R"({"rules":[{"nmae":"typo"}]})", "x").empty());
	CHECK(set.Rules().empty());
}

TEST(damage_and_execute)
{
	Outcome o;
	o.mode = Mode::Damage;
	o.damageFlat = 10.0f;
	o.damageWeaponMult = 2.0f;
	o.damageMaxHealthPct = 0.1f;
	o.executeBelowHealthPct = 0.25f;
	o.stagger = 0.5f;
	// 10 + 2*15 + 0.1*400 = 80 -> 300 - 80 = 220 > 100: survives, staggered
	auto v = Resolve(o, 300.0f, 400.0f, 15.0f);
	CHECK(!v.kill);
	CHECK_NEAR(v.damage, 80.0, 1e-4);
	CHECK_NEAR(v.stagger, 0.5, 1e-4);
	// 170 - 80 = 90 <= 100: executed
	v = Resolve(o, 170.0f, 400.0f, 15.0f);
	CHECK(v.kill);
	CHECK_NEAR(v.stagger, 0.0, 1e-4);
	// damage beyond the health: dead even without an execute threshold
	o.executeBelowHealthPct = 0.0f;
	CHECK(Resolve(o, 50.0f, 400.0f, 15.0f).kill);
}

TEST(kill_and_none_modes)
{
	Outcome o;
	CHECK(Resolve(o, 100.0f, 100.0f, 10.0f).kill);
	o.mode = Mode::None;
	const auto v = Resolve(o, 100.0f, 100.0f, 10.0f);
	CHECK(!v.kill);
	CHECK_NEAR(v.damage, 0.0, 1e-6);
}

TEST(pick_node_by_weight)
{
	const std::vector<NodeWeight> nodes{ { "head", 3.0f }, { "arm", 1.0f } };
	CHECK(PickNode(nodes, 0.0f) == "head");
	CHECK(PickNode(nodes, 0.74f) == "head");
	CHECK(PickNode(nodes, 0.76f) == "arm");
	CHECK(PickNode(nodes, 1.0f) == "arm");
	CHECK(PickNode({}, 0.5f).empty());
	CHECK(PickNode({ { "x", 0.0f } }, 0.5f).empty());
}

TEST(readme_examples_parse)
{
	std::ifstream     in("README.md", std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	std::string text = ss.str();
	std::erase(text, '\r');
	int         blocks = 0;
	std::size_t at = 0;
	while ((at = text.find("```json\n", at)) != std::string::npos) {
		const auto from = at + 8;
		const auto to = text.find("```", from);
		CHECK(to != std::string::npos);
		RuleSet    set;
		const auto error = set.AddFile(text.substr(from, to - from), "README");
		CHECK(error.empty());
		if (!error.empty()) {
			std::printf("    %s\n", error.c_str());
		}
		CHECK(!set.Rules().empty());
		++blocks;
		at = to + 3;
	}
	CHECK(blocks == 2);
}

TEST(one_shot_only_up_to_a_health)
{
	Outcome o;  // Kill
	o.oneShotMaxHealth = 500.0f;
	o.damageMaxHealthPct = 0.25f;
	o.executeBelowHealthPct = 0.2f;
	auto v = Resolve(o, 400.0f, 1000.0f, 10.0f);
	CHECK(v.kill && v.oneShot);
	v = Resolve(o, 900.0f, 1000.0f, 10.0f);  // too much: 250 damage, 650 left > 200
	CHECK(!v.kill);
	CHECK_NEAR(v.damage, 250.0, 1e-4);
	v = Resolve(o, 600.0f, 1000.0f, 10.0f);  // 350 left > 200 still
	CHECK(!v.kill);
	v = Resolve(o, 520.0f, 1000.0f, 10.0f);  // 270 left: executed? no (> 200)
	CHECK(!v.kill);
	o.executeBelowHealth = 300.0f;  // absolute execute
	v = Resolve(o, 520.0f, 1000.0f, 10.0f);
	CHECK(v.kill && !v.oneShot);
}

TEST(one_shot_up_to_a_share_of_max_health)
{
	Outcome o;
	o.oneShotMaxHealthPct = 0.5f;
	CHECK(Resolve(o, 500.0f, 1000.0f, 0.0f).oneShot);
	CHECK(!Resolve(o, 501.0f, 1000.0f, 0.0f).kill);  // no damage fields: nothing happens but the issen itself
}

TEST(player_perk_conditions)
{
	When w;
	w.playerPerksAny = { "Skyrim.esm|0x0BABE4", "Skyrim.esm|0x0BABE8" };
	w.playerPerksNone = { "Skyrim.esm|0x052D50" };
	auto s = Npc();
	CHECK(!Matches(w, s));
	s.playerPerks = { "skyrim.esm|0x0babe8" };
	CHECK(Matches(w, s));
	s.playerPerks.push_back("Skyrim.esm|0x052D50");
	CHECK(!Matches(w, s));
	When all;
	all.playerPerksAll = { "A", "B" };
	s.playerPerks = { "A" };
	CHECK(!Matches(all, s));
	s.playerPerks = { "B", "A" };
	CHECK(Matches(all, s));
}

TEST(perk_names_are_collected_once)
{
	RuleSet set;
	CHECK(set.AddFile(R"({"rules":[{"when":{"playerPerksAny":["P1","P2"]}},{"when":{"playerPerksNone":["p1"],"playerPerksAll":["P3"]}}]})", "x").empty());
	set.Finish();
	CHECK(set.PerkNames().size() == 3);
}

TEST(global_damage_replaces_the_numbers)
{
	GlobalDamage g;
	g.maxHealthPct = 0.4f;
	g.oneShotBelowPct = 0.5f;
	g.executeBelowPct = 0.2f;
	Outcome rule;
	rule.mode = Mode::Kill;
	rule.stagger = 0.7f;
	rule.dismember.chance = 0.5f;
	const auto o = WithGlobalDamage(rule, g, false);
	CHECK(o.dismember.chance == 0.5f);  // the rule keeps its dismemberment
	CHECK(Resolve(o, 400.0f, 1000.0f, 0.0f).oneShot);  // 40% <= 50%: dies outright
	auto v = Resolve(o, 900.0f, 1000.0f, 0.0f);  // 400 damage, 500 left > 200
	CHECK(!v.kill);
	CHECK_NEAR(v.damage, 400.0, 1e-3);
	CHECK_NEAR(v.stagger, 0.7, 1e-4);
	v = Resolve(o, 590.0f, 1000.0f, 0.0f);  // 190 left <= 200: executed
	CHECK(v.kill && !v.oneShot);
}

TEST(global_damage_for_bosses)
{
	GlobalDamage g;
	g.maxHealthPct = 0.4f;
	g.bossDamageMult = 0.5f;
	g.bossOneShotBelowPct = 0.0f;  // bosses never die outright
	g.bossExecuteBelowPct = 0.1f;
	const auto o = WithGlobalDamage(Outcome{}, g, true);
	CHECK(o.mode == Mode::Damage);
	const auto v = Resolve(o, 300.0f, 1000.0f, 0.0f);  // 200 damage, 100 left <= 100: executed
	CHECK_NEAR(v.damage, 200.0, 1e-3);
	CHECK(v.kill);
}

TEST(global_damage_keeps_immunity)
{
	Outcome none;
	none.mode = Mode::None;
	CHECK(WithGlobalDamage(none, GlobalDamage{}, false).mode == Mode::None);
}
