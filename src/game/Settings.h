#pragma once

#include "core/Detector.h"
#include "core/Rules.h"

#include <array>
#include <string>
#include <vector>

namespace issen::game
{
	// Data/SKSE/Plugins/IssenFramework.ini, read once at plugin load.
	struct Settings
	{
		DetectorConfig detector{};

		// [Input]
		bool             leftHand{ true };  // left attack counts while the left hand holds a melee weapon
		bool             unarmed{ false };  // attacks with fists count
		std::vector<int> extraKeyboard;     // more attack keys (e.g. a separate power attack key), DirectInput codes
		std::vector<int> extraMouse;
		std::vector<int> extraGamepad;

		// [Conditions]
		float maxDistance{ 300.0f };         // game units between the player and the attacker
		float maxAngle{ 75.0f };             // degrees off the player's facing
		float perfectMaxDistance{ 450.0f };  // perfect-dodge issen: the dodge carried the player away
		bool  perfectIgnoreAngle{ true };    // perfect-dodge issen: the attacker is usually beside or behind

		// [Perks] the issen is learned: without the perk (Plugin.esp|0xFormID or EditorID; empty = always) there is none
		std::string requirePerk{};         // the normal issen
		std::string requirePerkPerfect{};  // the issen after a perfect dodge

		// [Damage] one damage model for every enemy instead of the rules' numbers (the rules still decide immunity,
		// dismemberment and stagger)
		bool                damageGlobal{ false };
		rules::GlobalDamage damage{};

		// [Cost] stamina an issen takes (only when it fires)
		float staminaCost{ 20.0f };         // the normal issen
		float staminaCostPerfect{ 10.0f };  // the issen after a perfect dodge
		float staminaCostMaxPct{ 0.0f };    // plus this share of the player's maximum stamina (0..1)
		bool  staminaRequired{ true };      // not enough stamina: no issen (off: it fires and empties the bar)

		// [Execute]
		bool  execute{ true };         // false: detect and log only (stage 1 behaviour)
		float strikeDelay{ 0.10f };    // player seconds from the issen to the kill / damage
		bool  faceTarget{ true };      // turn the player to the enemy
		bool  dashOnPerfect{ true };   // perfect-dodge issen: the player is put next to the enemy first
		float dashStopDistance{ 90.0f };  // units in front of the enemy the dash ends
		bool  dismember{ true };       // Dismembering Framework, when it is installed and the rule asks for it

		// [Animation] the player's issen strike
		// graph events picked at random per issen: "event pitchDegrees rollDegrees" (turns the slash effects to match the
		// swing: a side power attack cuts flat, the standing one overhead); empty = keep the attack you pressed
		std::string animEvent{ "attackPowerStartRight 0 0, attackPowerStartInPlace -90 0" };
		bool        animStopFirst{ true };                 // send attackStop first so the event is taken mid-attack
		float       animSpeed{ 2.0f };                     // weaponSpeedMult while it plays
		float       animSpeedTime{ 0.6f };                 // player seconds the speed is held
		float       animSpeedAfterDash{ 1.3f };            // weaponSpeedMult when the strike follows a dash
		bool        strikeOnHitFrame{ true };              // kill / damage on the clip's HitFrame
		float       strikeMaxDelay{ 0.9f };               // ...or after this long when no HitFrame comes
		bool        lockPosition{ true };                 // the strike clip does not move the player
		float       lockAfter{ 0.35f };                   // player seconds the lock lasts past the strike

		// [Approach] before the strike: a back step (normal issen, Reactive Dodge Framework), vanish, dash, appear
		bool        preStep{ true };          // normal issen: step back first, then dash in
		float       preStepAngle{ 180.0f };   // RDF dodge angle relative to facing (180 = straight back)
		float       preStepTime{ 0.25f };     // player seconds of the step before the vanish
		float       vanishTime{ 0.12f };      // player seconds the dash to the enemy takes
		float       appearPause{ 0.15f };     // player seconds standing in front of the enemy before the strike
		std::string vanishShader{};  // e.g. Skyrim.esm|0x0EC355 TimeFadeOut01FXS (Alduin's time teleport)
		std::string appearShader{};  // e.g. Skyrim.esm|0x0EC354 TimeFadeIn01FXS
		std::string vanishArt{};     // e.g. Skyrim.esm|0x0EC353 TimeTeleportTargetFX
		std::string appearArt{};

		// [Ghost] silhouettes along the dash (GhostTrails mod)
		bool                 ghost{ true };
		int                  ghostCount{ 6 };
		float                ghostInterval{ 0.02f };  // game seconds between silhouettes
		float                ghostLifetime{ 0.45f };
		std::array<float, 4> ghostColor{ 0.55f, 0.8f, 1.0f, 0.6f };  // r, g, b, alpha
		float                ghostIntensity{ 2.5f };
		float                ghostRimPower{ 2.0f };
		float       approachFxTime{ 0.6f };   // seconds the shaders / art play

		// [Death] Onimusha's delayed death: the enemy stands, you sheathe, then it falls
		bool        deathDelayed{ true };
		float       deathDelay{ 0.9f };       // player seconds from the strike to the death
		bool        sheathe{ true };          // put the weapon away once the combat an issen kill was part of is over
		float       sheatheDelay{ 0.5f };     // seconds out of combat before it
		std::string sheatheSound{};           // e.g. Skyrim.esm|0x03C72F WPNBlade1HandSheatheSD (the sheathe clip has its own)
		float       killVerifyTime{ 0.5f };   // player seconds the death is waited for before a second try

		// [Camera]
		float hitstop{ 0.08f };       // real seconds the whole scene freezes on the strike; 0 = off
		float hitstopScale{ 0.03f };  // speed during the freeze
		float zoomFov{ 8.0f };        // degrees the third-person field of view narrows during the issen; 0 = off
		float zoomIn{ 0.10f };        // real seconds in
		float zoomOut{ 0.45f };       // real seconds out, after the issen

		// [SlowMotion] real seconds; 0 / 1 multipliers switch it off
		float slowWorld{ 0.15f };      // world speed during the hold
		float slowPlayer{ 1.0f };      // player speed during the hold
		float slowEaseIn{ 0.0f };
		float slowHold{ 0.35f };
		float slowEaseOut{ 0.30f };
		float saturation{ 0.0f };      // colour left during the slow motion (1 = full colour, 0 = black and white)

		// [Effects] "Plugin.esp|0xFormID" (local id), empty = off; lists are comma-separated
		std::string flashImod{ "Skyrim.esm|0x07E8FD" };  // alduinExplosionFlashImod, 0.25 s
		float       flashStrength{ 1.0f };
		// first one found: separated Slash Effects X's slash, else NPCKillSlash
		std::string strikeSound{ "separated Slash Effects X.esp|0x000802, Skyrim.esm|0x0DAB81" };
		std::string slowSound{ "Skyrim.esm|0x0ADE24" };  // VOCShoutFXSlowTimeIn
		// impact data sets played on the enemy at the strike (separated Slash Effects X: special slash, sparks slash)
		std::string strikeImpacts{ "" };
		std::string strikeImpactNode{ "NPC Spine2 [Spn2]" };
		// .nif effects (path under meshes\) shown on the enemy at the strike, as art objects
		std::string strikeArt{ "" };
		float       strikeArtDuration{ 1.0f };
		// .nif effects spawned in the world at the strike (what Mu Impact Framework does): "path scale rollDegrees", the
		// effect turned to the swing (the player's heading) and placed at the enemy's sImpactNode
		std::string strikeVfx{ "effects\\impacteffects\\AP05\\ssex_StandardSlash.nif 2.6 0 90, effects\\impacteffects\\AP05\\ssex_specialslash.nif 1.8 0 90, effects\\impacteffects\\AP05\\ssex_SparksSlash.nif 1.0 0, effects\\impacteffects\\AP05\\ssex_SlashImpact.nif 1.0 0, effects\\impacteffects\\AP05\\ssex_Sparks.nif 1.0 0, effects\\impacteffects\\AP05\\ssex_RingOfFire.nif 1.0 0, effects\\impacteffects\\AP05\\ssex_Glow.nif 1.0 0" };
		float       strikeVfxLifetime{ 1.5f };

		// [Cue] telegraph flash / glint on the enemy when an issen window opens
		bool        cueEnabled{ true };
		bool        cueOnSwing{ true };         // flash during the enemy's swing right before the hit
		bool        cueOnPerfectDodge{ true };  // flash when the perfect-dodge issen window opens
		bool        cueOnWeapon{ true };        // place on the enemy's weapon node (falls back to cueNode when unarmed)
		std::string cueNode{ "NPC Spine2 [Spn2]" };
		std::string cueVfx{ "effects\\impacteffects\\AP05\\ssex_Glow.nif 1.2 0 0, effects\\impacteffects\\AP05\\ssex_Sparks.nif 0.8 0 0" };
		float       cueVfxLifetime{ 0.35f };
		std::string cueArt{ "" };
		float       cueArtDuration{ 0.35f };
		std::string cueShader{ "" };
		float       cueShaderDuration{ 0.35f };
		std::string cueSound{ "" };

		// [Compat]
		bool        usePrecision{ true };
		bool        perfectDodge{ true };
		bool        realHit{ true };         // the strike is a real weapon hit of the game, its damage set by the issen
		bool        sendHitEvent{ true };    // the strike sends a hit event: Papyrus OnHit and other plugins' hit sinks see it
		bool        hitPowerAttack{ true };  // ...flagged as a power attack (abPowerAttack)
		bool        hitSpells{ true };       // perks' "Apply Combat Hit Spell" and the weapon's enchantment, as a real hit would
		std::string rdfModEvent{ "RDF_PerfectDodge" };

		// [Dev]
		bool devLog{ true };

		static const Settings& Get();
		static void            Load();
		// Main thread: replaces the live settings (the in-game menu).
		static void            Commit(const Settings& s);
	};
}
