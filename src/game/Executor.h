#pragma once

#include "core/Rules.h"

namespace issen::game::executor
{
	// kDataLoaded: resolves the effect forms named in the ini.
	void Init();
	// First new game / load: binds Dismembering Framework's API when it is installed.
	void BindDismembering();

	// Any thread: an issen against `target` was decided under `rule` (rules outlive the game session). Runs on the
	// next player tick: slow motion, flash and sound at once, the kill / damage after [Execute] fStrikeDelay.
	void Trigger(RE::Actor* target, rules::Kind kind, const rules::Rule* rule);

	// Any thread: plays the telegraph glint / glow on `target` when an issen window opens.
	void Cue(RE::Actor* target, rules::Kind kind);

	// Main thread, every player tick; `dt` is the player's frame time. PostUpdate runs after the game's own player
	// update (the game writes the attack speed graph variable there).
	void Update(float dt);
	void PostUpdate();
	// Any thread: this actor was struck by an issen and is about to die (its hits do not land, no second issen).
	bool IsDoomed(RE::Actor* actor);

	// Before a save: takes our temporary weapon speed off the player (so it is never saved).
	void RestoreSpeed();
	// Load / new game: drops pending strikes, ends our slow motion.
	void Reset();
}
