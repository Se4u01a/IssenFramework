#pragma once

#include "core/Rules.h"

namespace issen::game::enemy
{
	// Reads Data/SKSE/Plugins/IssenFramework/Rules/*.json (kDataLoaded).
	void Load();

	// The rule for an issen of `kind` against `actor`, or null (no rule matched: no issen). Any thread (read-only).
	const rules::Rule* Match(RE::Actor* actor, rules::Kind kind);

	// The actor stands on a Boss location ref type marker.
	bool IsBoss(RE::Actor* actor);

	// A perk named "Plugin.esp|0xFormID" or by EditorID (when a mod keeps perk EditorIDs). Null if not found.
	RE::BGSPerk* LookupPerk(const std::string& name);
	// Empty name: true. Otherwise the player has that perk (an unknown perk counts as missing).
	bool PlayerHasPerk(const std::string& name);
}
