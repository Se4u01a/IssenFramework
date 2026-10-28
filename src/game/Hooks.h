#pragma once

namespace issen::game::hooks
{
	// PlayerCharacter::Update (vfunc 0xAD): the clock. Plugin load.
	void InstallUpdate();

	// The game's melee hit (37673 + 0x3C0, the call Reactive Dodge Framework and Dismembering Framework hook too) and
	// Precision's pre-hit callback. Installed on the first new game / load, after every plugin's kDataLoaded, so this
	// hook is the outermost one on the call and sees the hit before the others do.
	void InstallHits();

	// A real melee hit by the attacker's right-hand weapon on the target, through the game's melee hit (37673): the
	// weapon's perks, enchantment and poison, the hit event (Papyrus OnHit), the hit reaction. Its damage is replaced by
	// `damage` and it is never blocked. False when the game dropped it before the damage (nothing was applied then).
	bool RealHit(RE::Actor* attacker, RE::Actor* target, float damage);

	// Attack buttons (BSInputDeviceManager sink). kDataLoaded.
	void InstallInput();

	// Reactive Dodge Framework's perfect-dodge mod event. kDataLoaded.
	void InstallPerfectDodge();
}
