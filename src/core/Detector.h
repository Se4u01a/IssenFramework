#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace issen
{
	enum class Hand : std::uint8_t
	{
		Right,
		Left,
		Extra,  // a key from [Input] sExtraKeys (e.g. a separate power attack key)
	};

	enum class ImpactSource : std::uint8_t
	{
		Vanilla,    // the game's melee hit (37673)
		Precision,  // Precision's pre-hit callback
	};

	// All times are seconds on the game-active clock.
	struct DetectorConfig
	{
		float window{ 0.12f };         // issen: the attack press came at most this long before the enemy's hit landed
		float earlyLog{ 0.50f };       // presses up to this long before a hit are reported as "early" (calibration)
		float lateLog{ 0.30f };        // presses up to this long after a hit are reported as "late" (calibration)
		float sameSwing{ 0.25f };      // a second hit report from the same attacker this soon is the same swing
		float perfectWindow{ 0.40f };  // after a perfect dodge, an attack press this soon is a perfect-dodge issen
		float mashGuard{ 0.30f };      // a press this soon after the previous one makes no issen (mashing the button)
	};

	struct ImpactVerdict
	{
		bool                 duplicate{ false };  // same swing as an earlier report (other source or a multi-hit)
		float                sinceFirst{ 0.0f };  // duplicate: time since the first report of the swing
		bool                 firstIssen{ false }; // duplicate: the swing's first report was an issen (cancel this one too)
		std::optional<float> lead;                // nearest unused press before the hit (within earlyLog): t_hit - t_press
		std::optional<Hand>  hand;
		bool                 issen{ false };      // lead <= window and eligible; that press is used up
		bool                 mashed{ false };     // in the window, but the press came right after another one
	};

	struct PressVerdict
	{
		enum class Kind : std::uint8_t
		{
			None,
			PerfectDodgeIssen,  // inside the window opened by a perfect dodge against `attacker`
			Late,               // after a hit that was not an issen (calibration)
		};
		Kind          kind{ Kind::None };
		float         delta{ 0.0f };  // PerfectDodgeIssen: since the perfect dodge; Late: since the hit
		std::uint32_t attacker{ 0 };
		bool          mashed{ false };  // inside a perfect-dodge window, but right after another press: no issen
	};

	// Decides whether an attack press and an enemy hit on the player make an issen. Pure: no engine types, the caller
	// supplies times and attacker ids. Each press makes at most one issen; each perfect dodge opens one window.
	class Detector
	{
	public:
		void                  Configure(const DetectorConfig& config) { config_ = config; }
		const DetectorConfig& Config() const { return config_; }

		PressVerdict  OnPress(double t, Hand hand);
		// `eligible`: the caller's conditions (distance, angle, rules) allow an issen against this attacker; when not,
		// the timing is still reported but the press stays unused.
		ImpactVerdict OnImpact(double t, std::uint32_t attacker, ImpactSource source, bool eligible = true);
		void          OnPerfectDodge(double t, std::uint32_t attacker);

		// Drops presses, hits and windows too old to matter at `now`.
		void Expire(double now);
		void Reset();

	private:
		struct Press
		{
			double t;
			Hand   hand;
			bool   used;
		};
		struct Impact
		{
			double        t;
			std::uint32_t attacker;
			bool          issen;
			bool          lateReported;
		};
		struct Perfect
		{
			double        t;
			std::uint32_t attacker;
		};

		DetectorConfig     config_{};
		std::deque<Press>  presses_;
		std::deque<Impact> impacts_;
		std::deque<Perfect> perfects_;
	};
}
