#include "Rules.h"

#include <glaze/json.hpp>

#include <algorithm>
#include <cctype>

template <>
struct glz::meta<issen::rules::Mode>
{
	using enum issen::rules::Mode;
	static constexpr auto value = glz::enumerate(Kill, Damage, None);
};

template <>
struct glz::meta<issen::rules::Kind>
{
	using enum issen::rules::Kind;
	static constexpr auto value = glz::enumerate(Normal, PerfectDodge);
};

namespace issen::rules
{
	namespace
	{
		constexpr glz::opts kRead{ .comments = true, .error_on_unknown_keys = true };

		bool EqualNoCase(const std::string& a, const std::string& b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		bool Has(const std::vector<std::string>& list, const std::string& value)
		{
			return std::ranges::any_of(list, [&](const std::string& item) { return EqualNoCase(item, value); });
		}
	}

	std::string RuleSet::AddFile(const std::string& text, const std::string& origin)
	{
		File file;
		if (const auto error = glz::read<kRead>(file, text)) {
			return glz::format_error(error, text);
		}
		for (auto& rule : file.rules) {
			if (rule.name.empty()) {
				rule.name = origin + "#" + std::to_string(&rule - file.rules.data());
			}
			rules_.push_back(std::move(rule));
		}
		return {};
	}

	void RuleSet::Finish()
	{
		std::ranges::stable_sort(rules_, [](const Rule& a, const Rule& b) { return a.priority > b.priority; });
	}

	const Rule* RuleSet::Match(const Subject& subject) const
	{
		for (const auto& rule : rules_) {
			if (rule.enabled && Matches(rule.when, subject)) {
				return &rule;
			}
		}
		return nullptr;
	}

	bool Matches(const When& when, const Subject& subject)
	{
		const auto has = [&](const std::string& kw) { return Has(subject.keywords, kw); };
		if (!when.keywordsAny.empty() && !std::ranges::any_of(when.keywordsAny, has)) {
			return false;
		}
		if (!std::ranges::all_of(when.keywordsAll, has)) {
			return false;
		}
		if (std::ranges::any_of(when.keywordsNone, has)) {
			return false;
		}
		if (!when.racesAny.empty() && !Has(when.racesAny, subject.race)) {
			return false;
		}
		if (when.boss && *when.boss != subject.boss) {
			return false;
		}
		if (when.unique && *when.unique != subject.unique) {
			return false;
		}
		if (subject.level < when.levelMin || (when.levelMax > 0 && subject.level > when.levelMax)) {
			return false;
		}
		if (when.kind && *when.kind != subject.kind) {
			return false;
		}
		const auto hasPerk = [&](const std::string& p) { return Has(subject.playerPerks, p); };
		if (!when.playerPerksAny.empty() && !std::ranges::any_of(when.playerPerksAny, hasPerk)) {
			return false;
		}
		if (!std::ranges::all_of(when.playerPerksAll, hasPerk)) {
			return false;
		}
		if (std::ranges::any_of(when.playerPerksNone, hasPerk)) {
			return false;
		}
		return true;
	}

	std::vector<std::string> RuleSet::PerkNames() const
	{
		std::vector<std::string> out;
		for (const auto& r : rules_) {
			for (const auto* list : { &r.when.playerPerksAny, &r.when.playerPerksAll, &r.when.playerPerksNone }) {
				for (const auto& p : *list) {
					if (!Has(out, p)) {
						out.push_back(p);
					}
				}
			}
		}
		return out;
	}

	Verdict Resolve(const Outcome& outcome, float health, float maxHealth, float weaponDamage)
	{
		Verdict v;
		switch (outcome.mode) {
		case Mode::Kill:
			{
				const bool tooMuch = (outcome.oneShotMaxHealth > 0.0f && health > outcome.oneShotMaxHealth) ||
				                     (outcome.oneShotMaxHealthPct > 0.0f && maxHealth > 0.0f && health > outcome.oneShotMaxHealthPct * maxHealth);
				if (!tooMuch) {
					v.kill = true;
					v.oneShot = true;
					return v;
				}
				break;  // too much health for one shot: the damage fields
			}
		case Mode::None:
			return v;
		case Mode::Damage:
			break;
		}
		v.damage = std::max(0.0f, outcome.damageFlat + outcome.damageWeaponMult * weaponDamage + outcome.damageMaxHealthPct * maxHealth);
		const float left = health - v.damage;
		v.kill = left <= 0.0f || (maxHealth > 0.0f && left <= outcome.executeBelowHealthPct * maxHealth) ||
		         (outcome.executeBelowHealth > 0.0f && left <= outcome.executeBelowHealth);
		v.stagger = v.kill ? 0.0f : std::clamp(outcome.stagger, 0.0f, 1.0f);
		return v;
	}

	Outcome WithGlobalDamage(const Outcome& rule, const GlobalDamage& g, bool boss)
	{
		if (rule.mode == Mode::None) {
			return rule;
		}
		Outcome o = rule;
		const float mult = boss ? g.bossDamageMult : 1.0f;
		const float oneShot = boss ? g.bossOneShotBelowPct : g.oneShotBelowPct;
		o.damageFlat = g.flat * mult;
		o.damageWeaponMult = g.weaponMult * mult;
		o.damageMaxHealthPct = g.maxHealthPct * mult;
		o.executeBelowHealthPct = boss ? g.bossExecuteBelowPct : g.executeBelowPct;
		o.executeBelowHealth = 0.0f;
		o.oneShotMaxHealth = 0.0f;
		if (oneShot > 0.0f) {
			o.mode = Mode::Kill;  // dies outright up to that share, takes the damage above it
			o.oneShotMaxHealthPct = std::min(oneShot, 1.0f);
		} else {
			o.mode = Mode::Damage;
			o.oneShotMaxHealthPct = 0.0f;
		}
		return o;
	}

	std::string PickNode(const std::vector<NodeWeight>& nodes, float roll)
	{
		float total = 0.0f;
		for (const auto& n : nodes) {
			total += std::max(0.0f, n.weight);
		}
		if (total <= 0.0f) {
			return {};
		}
		float at = std::clamp(roll, 0.0f, 0.999999f) * total;
		for (const auto& n : nodes) {
			const float w = std::max(0.0f, n.weight);
			if (at < w) {
				return n.node;
			}
			at -= w;
		}
		return nodes.back().node;
	}
}
