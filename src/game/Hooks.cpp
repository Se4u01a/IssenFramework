#include "Hooks.h"

#include "Issen.h"
#include "PrecisionAPI.h"
#include "Settings.h"

#include <algorithm>

namespace issen::game::hooks
{
	namespace
	{
		// ---- PlayerCharacter::Update
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* player, float delta)
			{
				Tick(delta);
				func(player, delta);
				PostTick();
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		// ---- melee hits (verified in Reactive Dodge Framework, REVERSE_ENGINEERING.md)
		constexpr std::uint32_t  kMeleeHitFunctionId = 37673;
		constexpr std::ptrdiff_t kHitCallOffset = 0x3C0;

		using ProcessHit_t = void (*)(RE::Actor*, RE::HitData*);
		ProcessHit_t g_originalHit = nullptr;

		// The issen's own hit (RealHit), while the game's melee hit runs.
		struct PendingHit
		{
			RE::Actor* target;
			float      damage;
			bool       seen{ false };
		};
		PendingHit* g_pendingHit = nullptr;

		RE::Actor* ActorOf(const RE::ActorHandle& handle)
		{
			const auto ptr = handle.get();
			return ptr ? ptr.get() : nullptr;
		}

		void HitThunk(RE::Actor* victim, RE::HitData* hit)
		{
			if (g_pendingHit && hit && victim == g_pendingHit->target) {
				g_pendingHit->seen = true;
				hit->totalDamage = g_pendingHit->damage;
				hit->percentBlocked = 0.0f;
				hit->flags.reset(RE::HitData::Flag::kBlocked, RE::HitData::Flag::kBlockWithWeapon, RE::HitData::Flag::kBlockCandidate);
				if (g_pendingHit->damage <= 0.0f) {
					hit->flags.reset(RE::HitData::Flag::kFatal, RE::HitData::Flag::kDismemberLimb, RE::HitData::Flag::kExplodeLimb);
				}
			}
			if (hit && victim && victim->IsPlayerRef() && OnEnemyHit(ActorOf(hit->aggressor), ImpactSource::Vanilla, hit)) {
				return;  // answered by an issen: the hit never lands (RDF and DF, hooked inside this one, do not see it)
			}
			g_originalHit(victim, hit);
		}

		void InstallPrecision()
		{
			auto* api = static_cast<PRECISION_API::IVPrecision4*>(PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V4));
			if (!api) {
				logger::info("Precision not found; hits come from the game's hit hook only");
				return;
			}
			const auto result = api->AddPreHitCallback(SKSE::GetPluginHandle(), [](const PRECISION_API::PrecisionHitData& hit) {
				PRECISION_API::PreHitCallbackReturn ret;
				if (hit.target && hit.target->IsPlayerRef()) {
					ret.bIgnoreHit = OnEnemyHit(hit.attacker, ImpactSource::Precision, nullptr);
				} else if (hit.attacker && hit.attacker->IsPlayerRef() && Settings::Get().devLog) {
					// Diagnostics: shows the callback fires at all when no enemy hit ever comes through it.
					logger::info("Precision: player hit {:08X}", hit.target ? hit.target->GetFormID() : 0u);
				}
				return ret;
			});
			logger::info("Precision pre-hit callback: {}", result == PRECISION_API::APIResult::OK ? "registered" : "already registered");
		}

		// ---- attack buttons
		bool IsMapped(const RE::BSFixedString& userEvent, RE::INPUT_DEVICE device, std::uint32_t code)
		{
			const auto* map = RE::ControlMap::GetSingleton();
			if (!map) {
				return false;
			}
			const auto key = map->GetMappedKey(userEvent, device);
			return key != RE::ControlMap::kInvalid && key == code;
		}

		bool IsExtraKey(RE::INPUT_DEVICE device, std::uint32_t code)
		{
			const auto& s = Settings::Get();
			const auto  has = [code](const std::vector<int>& codes) { return std::ranges::find(codes, static_cast<int>(code)) != codes.end(); };
			switch (device) {
			case RE::INPUT_DEVICE::kKeyboard: return has(s.extraKeyboard);
			case RE::INPUT_DEVICE::kMouse: return has(s.extraMouse);
			case RE::INPUT_DEVICE::kGamepad: return has(s.extraGamepad);
			default: return false;
			}
		}

		// The hand swings a melee weapon, or a fist with bUnarmed. An empty left hand next to a weapon blocks.
		bool HandAttacks(RE::PlayerCharacter* player, bool left)
		{
			const bool  unarmed = Settings::Get().unarmed;
			const auto* form = player->GetEquippedObject(left);
			if (!form) {
				return unarmed && (!left || !player->GetEquippedObject(false));
			}
			const auto* weapon = form->As<RE::TESObjectWEAP>();
			if (!weapon) {
				return false;  // shield, spell, torch
			}
			if (weapon->IsHandToHandMelee()) {
				return unarmed;
			}
			return weapon->IsMelee();
		}

		std::optional<Hand> AttackOf(const RE::ButtonEvent& b)
		{
			const auto* events = RE::UserEvents::GetSingleton();
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const auto* state = player ? player->AsActorState() : nullptr;
			if (!events || !state || !state->IsWeaponDrawn()) {
				return std::nullopt;
			}
			const auto device = b.GetDevice();
			const auto code = b.GetIDCode();
			if (IsMapped(events->rightAttack, device, code)) {
				return HandAttacks(player, false) ? std::optional{ Hand::Right } : std::nullopt;
			}
			if (IsMapped(events->leftAttack, device, code)) {
				return Settings::Get().leftHand && HandAttacks(player, true) ? std::optional{ Hand::Left } : std::nullopt;
			}
			if (IsExtraKey(device, code)) {
				return HandAttacks(player, false) ? std::optional{ Hand::Extra } : std::nullopt;
			}
			return std::nullopt;
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				auto* ui = RE::UI::GetSingleton();
				if (!events || !ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto* e = *events; e; e = e->next) {
					if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) {
						continue;
					}
					const auto* b = e->AsButtonEvent();
					if (!b->IsDown()) {
						continue;
					}
					if (const auto hand = AttackOf(*b)) {
						OnAttackPress(*hand);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		InputSink g_inputSink;

		// ---- RDF perfect dodge: sender = attacker, strArg = the dodger's FormID (hex)
		class PerfectDodgeSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				if (!event || event->eventName != eventName_) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto dodger = std::strtoul(event->strArg.c_str(), nullptr, 16);
				if (dodger != 0x14) {
					return RE::BSEventNotifyControl::kContinue;
				}
				OnPerfectDodge(event->sender ? event->sender->As<RE::Actor>() : nullptr);
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSFixedString eventName_;
		};

		PerfectDodgeSink g_perfectSink;
	}

	void InstallUpdate()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		PlayerUpdate::func = vtbl.write_vfunc(0xAD, PlayerUpdate::thunk);
		logger::info("PlayerCharacter::Update hooked");
	}

	void InstallHits()
	{
		static bool installed = false;
		if (installed) {
			return;
		}
		installed = true;

		const REL::Relocation<std::uintptr_t> function{ REL::ID(kMeleeHitFunctionId) };
		const auto                           site = function.address() + kHitCallOffset;
		const auto*                          bytes = reinterpret_cast<const std::uint8_t*>(site);
		if (bytes[0] != 0xE8) {
			logger::error("hit hook: expected CALL (E8) at {:#x}, found {:02X}; melee hits will not be seen", site, bytes[0]);
		} else {
			REL::Relocation<std::uintptr_t> callSite{ site };
			g_originalHit = reinterpret_cast<ProcessHit_t>(callSite.write_call<5>(HitThunk));
			logger::info("hit hook installed at {:#x} (chained to {:#x})", site, reinterpret_cast<std::uintptr_t>(g_originalHit));
		}
		if (Settings::Get().usePrecision) {
			InstallPrecision();
		}
	}

	bool RealHit(RE::Actor* attacker, RE::Actor* target, float damage)
	{
		if (!g_originalHit || !attacker || !target) {
			return false;  // without the hook the damage could not be set
		}
		using MeleeHit_t = void (*)(RE::Actor* attacker, RE::Actor* target, RE::Projectile* projectile, bool leftHand);
		static REL::Relocation<MeleeHit_t> meleeHit{ REL::ID(kMeleeHitFunctionId) };
		PendingHit pending{ target, damage };
		g_pendingHit = &pending;
		meleeHit(attacker, target, nullptr, false);
		g_pendingHit = nullptr;
		return pending.seen;
	}

	void InstallInput()
	{
		auto* devices = RE::BSInputDeviceManager::GetSingleton();
		if (!devices) {
			logger::error("input: BSInputDeviceManager not available, attack presses will not be seen");
			return;
		}
		devices->AddEventSink(&g_inputSink);
		logger::info("input sink registered");
	}

	void InstallPerfectDodge()
	{
		const auto& s = Settings::Get();
		if (!s.perfectDodge || s.rdfModEvent.empty()) {
			return;
		}
		if (!::GetModuleHandleW(L"ReactiveDodgeFramework.dll")) {
			logger::info("Reactive Dodge Framework not loaded: no perfect-dodge issen");
			return;
		}
		auto* source = SKSE::GetModCallbackEventSource();
		if (!source) {
			logger::error("mod event source not available: no perfect-dodge issen");
			return;
		}
		g_perfectSink.eventName_ = s.rdfModEvent;
		source->AddEventSink(&g_perfectSink);
		logger::info("listening for '{}' from Reactive Dodge Framework", s.rdfModEvent);
	}
}
