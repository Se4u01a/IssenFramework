#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace issen::rules
{
	// What an issen does to the enemy it answers. Rule files: Data/SKSE/Plugins/IssenFramework/Rules/*.json, every
	// file's rules merged, the highest priority match wins (file order breaks ties).

	enum class Mode : std::uint8_t
	{
		Kill,    // dies at once
		Damage,  // takes damage; dies when that leaves it at or below executeBelowHealthPct
		None,    // no issen against it: the hit lands as usual
	};

	enum class Kind : std::uint8_t
	{
		Normal,        // attack pressed just before the enemy's hit
		PerfectDodge,  // attack pressed right after a perfect dodge
	};

	struct When
	{
		std::vector<std::string> keywordsAny;   // keyword EditorIDs (actor base or race): at least one
		std::vector<std::string> keywordsAll;   // all of them
		std::vector<std::string> keywordsNone;  // none of them
		std::vector<std::string> racesAny;      // race EditorIDs
		std::optional<bool>      boss;          // location ref type Boss
		std::optional<bool>      unique;        // unique actor base
		std::uint16_t            levelMin{ 0 };
		std::uint16_t            levelMax{ 0 };  // 0 = no limit
		std::optional<Kind>      kind;
		// the player's perks: "Plugin.esp|0xFormID" (or an EditorID when a mod keeps them, e.g. po3's Tweaks)
		std::vector<std::string> playerPerksAny;
		std::vector<std::string> playerPerksAll;
		std::vector<std::string> playerPerksNone;
	};

	struct NodeWeight
	{
		std::string node;  // skeleton node name, e.g. "NPC Head [Head]"
		float       weight{ 1.0f };
	};

	struct Dismember
	{
		bool                    enabled{ true };
		float                   chance{ 1.0f };  // 0..1, only when the issen kills
		std::vector<NodeWeight> nodes;           // one is picked by weight
	};

	struct Outcome
	{
		Mode      mode{ Mode::Kill };
		float     damageFlat{ 0.0f };
		float     damageWeaponMult{ 0.0f };       // x the player's weapon base damage
		float     damageMaxHealthPct{ 0.0f };     // x the enemy's maximum health (0..1)
		float     executeBelowHealthPct{ 0.0f };  // after the damage, at or below this share of max health: kill
		float     executeBelowHealth{ 0.0f };     // ...or at or below this much health; 0 = off
		// Kill only: one shot only while the enemy has at most this much health / this share of its maximum (0 = no
		// limit); above it the damage fields apply, with the execute thresholds
		float     oneShotMaxHealth{ 0.0f };
		float     oneShotMaxHealthPct{ 0.0f };
		float     stagger{ 0.0f };                // stagger magnitude 0..1 when it survives; 0 = none
		Dismember dismember{};
	};

	struct Rule
	{
		std::string name;
		bool        enabled{ true };
		int         priority{ 0 };
		When        when{};
		Outcome     outcome{};
	};

	struct File
	{
		std::vector<Rule> rules;
	};

	// The enemy as the rules see it (filled by the game layer).
	struct Subject
	{
		std::vector<std::string> keywords;
		std::string              race;
		std::uint16_t            level{ 1 };
		bool                     boss{ false };
		bool                     unique{ false };
		Kind                     kind{ Kind::Normal };
		std::vector<std::string> playerPerks;  // the perk names the rules use that the player has
	};

	struct Verdict
	{
		bool  kill{ false };
		bool  oneShot{ false };  // killed by the Kill mode itself (not by damage or an execute)
		float damage{ 0.0f };  // applied when it survives
		float stagger{ 0.0f };
	};

	class RuleSet
	{
	public:
		// Parses one file's text and adds its rules. Returns an error message, empty on success.
		std::string AddFile(const std::string& text, const std::string& origin);
		// Orders the rules by priority (stable). Call after the last AddFile.
		void Finish();

		const Rule*              Match(const Subject& subject) const;
		const std::vector<Rule>& Rules() const { return rules_; }
		// Every perk name the rules use (the game resolves these once and reports which the player has).
		std::vector<std::string> PerkNames() const;

	private:
		std::vector<Rule> rules_;
	};

	// One damage model for every enemy (the in-game menu's "Damage" page), replacing the numbers of a rule's outcome. The
	// rule still decides who is immune (mode None), the dismemberment and the stagger.
	struct GlobalDamage
	{
		float flat{ 0.0f };
		float weaponMult{ 0.0f };
		float maxHealthPct{ 0.35f };    // damage = share of the enemy's maximum health
		float oneShotBelowPct{ 0.5f };  // the enemy dies outright while its health is at most this share of its maximum
		float executeBelowPct{ 0.3f };  // ...or when the damage leaves it at most this share
		float bossDamageMult{ 1.0f };   // bosses (location ref type Boss): damage times this
		float bossOneShotBelowPct{ 0.25f };
		float bossExecuteBelowPct{ 0.25f };
	};
	Outcome WithGlobalDamage(const Outcome& rule, const GlobalDamage& g, bool boss);

	bool    Matches(const When& when, const Subject& subject);
	Verdict Resolve(const Outcome& outcome, float health, float maxHealth, float weaponDamage);
	// Picks a node by weight; `roll` in [0, 1). Empty when there is none.
	std::string PickNode(const std::vector<NodeWeight>& nodes, float roll);
}
