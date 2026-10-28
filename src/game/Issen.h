#pragma once

#include "core/Detector.h"

namespace issen::game
{
	// The game side of the detector: one clock and one Detector for the player, guarded by a lock (input sink, hit hook,
	// Precision and the mod-event sink may report from different threads). An issen is handed to the executor ([Execute] bEnabled).

	// Applies Settings::Get().detector (plugin load).
	void Configure();

	// PlayerCharacter::Update: advances the clock (paused frames do not count).
	void Tick(float dt);
	// After the game's own player update.
	void PostTick();
	void Reset();

	void OnAttackPress(Hand hand);
	// `hit` is the game's hit data when the report comes from the melee hit hook, null from Precision.
	// Returns true when the hit must not land (it was answered by an issen).
	bool OnEnemyHit(RE::Actor* attacker, ImpactSource source, const RE::HitData* hit);
	void OnPerfectDodge(RE::Actor* attacker);
}
