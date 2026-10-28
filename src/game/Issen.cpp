#include "Issen.h"

#include "EnemyRules.h"
#include "Executor.h"
#include "Settings.h"
#include "core/Clock.h"

#include <cmath>
#include <mutex>

namespace issen::game
{
	namespace
	{
		std::mutex g_lock;
		Clock      g_clock;
		Detector   g_detector;

		template <class... Args>
		void DevLog(spdlog::format_string_t<Args...> format, Args&&... args)
		{
			if (Settings::Get().devLog) {
				logger::info(format, std::forward<Args>(args)...);
			}
		}

		std::uint32_t HandleOf(RE::TESObjectREFR* ref)
		{
			return ref ? ref->GetHandle().native_handle() : 0;
		}

		const char* HandName(Hand hand)
		{
			switch (hand) {
			case Hand::Right: return "right";
			case Hand::Left: return "left";
			default: return "extra";
			}
		}

		const char* SourceName(ImpactSource source)
		{
			return source == ImpactSource::Precision ? "Precision" : "vanilla";
		}

		// The log is UTF-8. Game strings usually are too (translated string files); older plugins may carry the
		// system code page (cp1251), which is converted.
		std::string ToUtf8(const char* text)
		{
			if (!text || !*text) {
				return {};
			}
			if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0) > 0) {
				return text;
			}
			const int wide = ::MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
			if (wide <= 0) {
				return text;
			}
			std::wstring w(static_cast<std::size_t>(wide), L'\0');
			::MultiByteToWideChar(CP_ACP, 0, text, -1, w.data(), wide);
			const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (bytes <= 0) {
				return text;
			}
			std::string out(static_cast<std::size_t>(bytes), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), bytes, nullptr, nullptr);
			out.pop_back();  // the terminator
			return out;
		}

		std::string Describe(RE::TESObjectREFR* ref)
		{
			if (!ref) {
				return "<none>";
			}
			return std::format("{:08X} '{}'", ref->GetFormID(), ToUtf8(ref->GetName()));
		}

		// Arrows and bolts also reach the melee hit call; an issen answers melee swings and bashes only.
		bool IsMeleeHit(const RE::Actor* attacker, const RE::HitData* hit)
		{
			using Flag = RE::HitData::Flag;
			if (hit && hit->flags.any(Flag::kMeleeAttack, Flag::kBash, Flag::kTimedBash)) {
				return true;
			}
			const auto* state = attacker->AsActorState();
			const auto  attack = state ? state->GetAttackState() : RE::ATTACK_STATE_ENUM::kNone;
			return attack >= RE::ATTACK_STATE_ENUM::kDraw && attack <= RE::ATTACK_STATE_ENUM::kBash;
		}

		// The strike is the right hand's: at the moment the issen fires it must hold a melee weapon (fists only with
		// bUnarmed). Bows, crossbows, staffs, spells and a weapon swapped out since the press give no issen.
		// Empty when it does, else why not.
		std::string StrikeWeaponBlocked(RE::PlayerCharacter* player)
		{
			const auto* form = player ? player->GetEquippedObject(false) : nullptr;
			if (!form) {
				return Settings::Get().unarmed ? std::string{} : std::string{ "fists in the right hand ([Input] bUnarmed off)" };
			}
			const auto* weapon = form->As<RE::TESObjectWEAP>();
			if (!weapon) {
				return std::format("the right hand holds no weapon ({})", ToUtf8(form->GetName()));
			}
			if (weapon->IsHandToHandMelee()) {
				return Settings::Get().unarmed ? std::string{} : std::string{ "fists in the right hand ([Input] bUnarmed off)" };
			}
			if (!weapon->IsMelee()) {
				return std::format("the right hand holds a ranged weapon or a staff ({})", ToUtf8(weapon->GetName()));
			}
			return {};
		}

		// [Cost] what an issen of this kind costs the player right now.
		float StaminaCost(RE::PlayerCharacter* player, rules::Kind kind)
		{
			const auto& s = Settings::Get();
			const float flat = kind == rules::Kind::PerfectDodge ? s.staminaCostPerfect : s.staminaCost;
			const float max = player ? player->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kStamina) : 0.0f;
			return std::max(0.0f, flat + s.staminaCostMaxPct * max);
		}

		float Stamina(RE::PlayerCharacter* player)
		{
			return player ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina) : 0.0f;
		}

		void PayStamina(RE::PlayerCharacter* player, rules::Kind kind)
		{
			const float cost = StaminaCost(player, kind);
			if (player && cost > 0.0f) {
				const float before = Stamina(player);
				player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, std::min(cost, std::max(before, 0.0f)));
				DevLog("  stamina {:.0f} -> {:.0f} (cost {:.0f})", before, Stamina(player), cost);
			}
		}

		// Where the attacker stands relative to the player, for the conditions the executor will check.
		struct Geometry
		{
			float distance{ 0.0f };
			float angle{ 0.0f };  // degrees off the player's facing, 0..180
			bool  ok{ false };
		};

		Geometry Measure(RE::PlayerCharacter* player, RE::Actor* attacker, rules::Kind kind)
		{
			Geometry g;
			if (!player || !attacker) {
				return g;
			}
			const auto& s = Settings::Get();
			g.distance = player->GetPosition().GetDistance(attacker->GetPosition());
			g.angle = std::fabs(player->GetHeadingAngle(attacker->GetPosition(), false));
			if (kind == rules::Kind::PerfectDodge) {
				g.ok = g.distance <= s.perfectMaxDistance && (s.perfectIgnoreAngle || g.angle <= s.maxAngle);
			} else {
				g.ok = g.distance <= s.maxDistance && g.angle <= s.maxAngle;
			}
			return g;
		}

		// Why an issen against this attacker would not fire (empty: it would), and the rule that applies.
		struct Eligibility
		{
			const rules::Rule* rule{ nullptr };
			std::string        blocked;
		};

		Eligibility Check(RE::Actor* attacker, const Geometry& geometry, rules::Kind kind)
		{
			Eligibility e;
			if (!attacker || attacker->IsDead()) {
				e.blocked = "attacker gone";
				return e;
			}
			if (executor::IsDoomed(attacker)) {
				e.blocked = "already struck by an issen";
				return e;
			}
			const auto& perk = kind == rules::Kind::PerfectDodge ? Settings::Get().requirePerkPerfect : Settings::Get().requirePerk;
			if (!enemy::PlayerHasPerk(perk)) {
				e.blocked = std::format("the player lacks the perk '{}' ([Perks])", perk);
				return e;
			}
			if (Settings::Get().staminaRequired) {
				auto*       player = RE::PlayerCharacter::GetSingleton();
				const float cost = StaminaCost(player, kind);
				if (cost > 0.0f && Stamina(player) < cost) {
					e.blocked = std::format("not enough stamina ({:.0f} of {:.0f}, [Cost])", Stamina(player), cost);
					return e;
				}
			}
			if (!geometry.ok) {
				e.blocked = std::format("outside the conditions (distance {:.0f}, angle {:.0f})", geometry.distance, geometry.angle);
				return e;
			}
			e.rule = enemy::Match(attacker, kind);
			if (!e.rule) {
				e.blocked = "no rule matched";
			} else if (e.rule->outcome.mode == rules::Mode::None) {
				e.blocked = std::format("rule '{}' says none", e.rule->name);
			}
			return e;
		}
	}

	void Configure()
	{
		std::scoped_lock lock(g_lock);
		g_detector.Configure(Settings::Get().detector);
	}

	void Tick(float dt)
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->GameIsPaused()) {
			return;
		}
		{
			std::scoped_lock lock(g_lock);
			g_clock.Advance(dt);
			g_detector.Expire(g_clock.Now());
		}
		executor::Update(dt);
	}

	void PostTick()
	{
		auto* ui = RE::UI::GetSingleton();
		if (ui && !ui->GameIsPaused()) {
			executor::PostUpdate();
		}
	}

	void Reset()
	{
		{
			std::scoped_lock lock(g_lock);
			g_detector.Reset();
		}
		executor::Reset();
		logger::info("session start: detector reset");
	}

	void OnAttackPress(Hand hand)
	{
		PressVerdict  verdict;
		double        now = 0.0;
		std::uint64_t frame = 0;
		{
			std::scoped_lock lock(g_lock);
			now = g_clock.Now();
			frame = g_clock.Frame();
			verdict = g_detector.OnPress(now, hand);
		}
		DevLog("press {} at t={:.3f} (frame {})", HandName(hand), now, frame);

		if (verdict.mashed) {
			logger::info("issen (perfect dodge) not fired: the press came right after another one (mashing, [Window] fMashGuard {:.2f}s)",
				g_detector.Config().mashGuard);
			return;
		}
		if (verdict.kind == PressVerdict::Kind::None) {
			return;
		}
		const auto ref = RE::TESObjectREFR::LookupByHandle(verdict.attacker);
		auto*      attacker = ref ? ref->As<RE::Actor>() : nullptr;
		if (verdict.kind == PressVerdict::Kind::PerfectDodgeIssen) {
			const auto geometry = Measure(RE::PlayerCharacter::GetSingleton(), attacker, rules::Kind::PerfectDodge);
			const auto e = Check(attacker, geometry, rules::Kind::PerfectDodge);
			if (!e.blocked.empty()) {
				logger::info("issen (perfect dodge) vs {} not fired: {}", Describe(ref.get()), e.blocked);
				return;
			}
			if (const auto why = StrikeWeaponBlocked(RE::PlayerCharacter::GetSingleton()); !why.empty()) {
				logger::info("issen (perfect dodge) vs {} not fired: {}", Describe(ref.get()), why);
				return;
			}
			logger::info("ISSEN (perfect dodge) vs {}: press {:.3f}s after the dodge, distance {:.0f}, angle {:.0f}, rule '{}'",
				Describe(ref.get()), verdict.delta, geometry.distance, geometry.angle, e.rule->name);
			if (Settings::Get().execute) {
				PayStamina(RE::PlayerCharacter::GetSingleton(), rules::Kind::PerfectDodge);
				executor::Trigger(attacker, rules::Kind::PerfectDodge, e.rule);
			}
		} else {
			DevLog("  late by {:.3f}s for the hit of {}", verdict.delta, Describe(ref.get()));
		}
	}

	bool OnEnemyHit(RE::Actor* attacker, ImpactSource source, const RE::HitData* hit)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!attacker || !player || attacker == player) {
			return false;
		}
		if (!IsMeleeHit(attacker, hit)) {
			const auto* state = attacker->AsActorState();
			DevLog("hit by {} via {}: not a melee swing (attacker state {}, flags {:#x}), ignored", Describe(attacker), SourceName(source),
				state ? static_cast<int>(state->GetAttackState()) : -1, hit ? hit->flags.underlying() : 0u);
			return false;
		}
		const auto    geometry = Measure(player, attacker, rules::Kind::Normal);
		auto          eligibility = Check(attacker, geometry, rules::Kind::Normal);
		if (eligibility.blocked.empty()) {
			eligibility.blocked = StrikeWeaponBlocked(player);  // before the detector: a refused swing is not marked as an issen
		}
		ImpactVerdict verdict;
		double        now = 0.0;
		std::uint64_t frame = 0;
		{
			std::scoped_lock lock(g_lock);
			now = g_clock.Now();
			frame = g_clock.Frame();
			verdict = g_detector.OnImpact(now, HandleOf(attacker), source, eligibility.blocked.empty());
		}
		if (executor::IsDoomed(attacker)) {
			DevLog("hit by {} via {}: struck by an issen, about to die: cancelled", Describe(attacker), SourceName(source));
			return Settings::Get().execute;
		}
		if (verdict.duplicate) {
			DevLog("hit by {} via {} at t={:.3f}: same swing, {:.3f}s after its first report{}", Describe(attacker), SourceName(source), now,
				verdict.sinceFirst, verdict.firstIssen ? " (issen: cancelled too)" : "");
			return verdict.firstIssen && Settings::Get().execute;
		}

		const auto* state = attacker->AsActorState();
		const auto* playerState = player->AsActorState();
		std::string flags;
		if (hit) {
			flags = std::format(" damage {:.1f}{}{} (flags {:#x})", hit->totalDamage, hit->flags.any(RE::HitData::Flag::kPowerAttack) ? " power" : "",
				hit->flags.any(RE::HitData::Flag::kBlocked) ? " blocked" : "", hit->flags.underlying());
		}
		DevLog("hit by {} via {} at t={:.3f} (frame {}): distance {:.0f}, angle {:.0f}, attacker state {}, player state {}{}{}",
			Describe(attacker), SourceName(source), now, frame, geometry.distance, geometry.angle,
			state ? static_cast<int>(state->GetAttackState()) : -1, playerState ? static_cast<int>(playerState->GetAttackState()) : -1,
			player->IsBlocking() ? " blocking" : "", flags);

		if (!verdict.lead) {
			DevLog("  no attack press in the {:.2f}s before", g_detector.Config().earlyLog);
			return false;
		}
		if (verdict.issen) {
			logger::info("ISSEN vs {}: {} press {:.3f}s before the hit (window {:.3f}s), distance {:.0f}, angle {:.0f}, rule '{}'",
				Describe(attacker), HandName(*verdict.hand), *verdict.lead, g_detector.Config().window, geometry.distance, geometry.angle,
				eligibility.rule->name);
			if (!Settings::Get().execute) {
				return false;
			}
			PayStamina(player, rules::Kind::Normal);
			executor::Trigger(attacker, rules::Kind::Normal, eligibility.rule);
			return true;  // the enemy's hit does not land
		}
		if (verdict.mashed) {
			logger::info("issen vs {} not fired: the press came right after another one (mashing, [Window] fMashGuard {:.2f}s)",
				Describe(attacker), g_detector.Config().mashGuard);
		} else if (*verdict.lead <= g_detector.Config().window) {
			logger::info("issen vs {} not fired: {}", Describe(attacker), eligibility.blocked);
		} else {
			DevLog("  early by {:.3f}s: {} press {:.3f}s before the hit", *verdict.lead - g_detector.Config().window,
				HandName(*verdict.hand), *verdict.lead);
		}
		return false;
	}

	void OnPerfectDodge(RE::Actor* attacker)
	{
		double now = 0.0;
		{
			std::scoped_lock lock(g_lock);
			now = g_clock.Now();
			g_detector.OnPerfectDodge(now, HandleOf(attacker));
		}
		DevLog("perfect dodge against {} at t={:.3f}: issen window {:.2f}s open", Describe(attacker), now,
			Settings::Get().detector.perfectWindow);
	}
}
