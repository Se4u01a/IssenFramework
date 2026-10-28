#pragma once

namespace issen::game::ghost
{
	// Silhouettes of the player along the issen dash, through the GhostTrails mod's API (GhostTrails.dll).
	void Init();  // kDataLoaded
	// Warms the provider's clones for the player (an issen began; the dash follows shortly).
	void Prepare(RE::Actor* actor);
	// Starts the trail: [Ghost] iCount silhouettes, one every fInterval, each fading over fLifetime.
	void Spawn(RE::Actor* actor);
	// Every player tick: releases requests that are over.
	void Update(float dt);
	void Reset();
}
