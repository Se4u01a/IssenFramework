#include "Executor.h"

#include "DismemberingFrameworkAPI.h"
#include "EnemyRules.h"
#include "Ghost.h"
#include "Hooks.h"
#include "ReactiveDodgeAPI.h"
#include "Settings.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numbers>
#include <random>
#include <sstream>
#include <vector>

namespace issen::game::executor
{
	void RestoreSpeed();

	namespace
	{
		struct Request
		{
			std::uint32_t      target;
			rules::Kind        kind;
			const rules::Rule* rule;
		};

		struct Strike
		{
			std::uint32_t      target;
			const rules::Rule* rule;
			float              delay;     // player seconds left
			bool               onHitFrame;  // strike as soon as the player's clip reaches its HitFrame
		};

		// The player's HitFrame annotations (graph event sink, main thread).
		std::atomic<std::uint32_t> g_hitFrames{ 0 };
		std::uint32_t              g_hitFramesAtBegin{ 0 };

		class GraphSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* e, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (!e) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (tracing.load(std::memory_order_relaxed)) {
					logger::info("  player graph: {}{}{}", e->tag.c_str(), e->payload.empty() ? "" : " ", e->payload.c_str());
				}
				if (_stricmp(e->tag.c_str(), "HitFrame") == 0) {
					g_hitFrames.fetch_add(1, std::memory_order_relaxed);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			std::atomic<bool> tracing{ false };  // [Dev] bDevLog: log the player's graph events during an issen
		};
		GraphSink g_graphSink;
		float     g_traceLeft{ 0.0f };

		float g_speedLeft{ 0.0f };     // player seconds the strike speed is still held
		bool  g_speedRestore{ false }; // write the normal speed back once
		float g_speedDelta{ 0.0f };    // our temporary WeaponSpeedMult modifier on the player (taken off before a save)

		std::mutex           g_requestLock;
		std::vector<Request> g_requests;  // any thread -> main thread
		std::vector<Strike>  g_strikes;   // main thread

		// The part before the strike (main thread).
		enum class Phase : std::uint8_t
		{
			PreStep,  // the back step plays
			Vanish,   // dashing to the enemy (ghosts along the way)
			Arrived,  // in front of the enemy, a moment before the strike
		};
		struct Approach
		{
			std::uint32_t      target;
			const rules::Rule* rule;
			rules::Kind        kind;
			Phase              phase;
			float              timer;  // player seconds left in the phase
			bool               dash;
			RE::NiPoint3       from{};  // the dash
			RE::NiPoint3       to{};
		};
		std::vector<Approach> g_approaches;

		// Struck and about to die (delayed death), main thread; the handles are mirrored for IsDoomed (any thread).
		struct Death
		{
			std::uint32_t      target;
			const rules::Rule* rule;
			float              timer;          // player seconds to the death
			float              sheathe;        // player seconds to the sheathe; < 0 = done or off
			bool               killed{ false };
			float              verifyLeft{ 0.0f };
			int                tries{ 0 };
		};
		std::vector<Death>         g_deaths;
		bool                       g_sheatheAfterCombat{ false };  // an issen killed: sheathe once the combat is over
		float                      g_outOfCombat{ 0.0f };          // seconds the player has been out of combat

		// The player held where the strike began (its clip would carry it sideways).
		bool         g_lock{ false };
		RE::NiPoint3 g_lockAt{};
		float        g_lockAfter{ 0.0f };  // player seconds the lock still lasts once no strike is pending
		std::mutex                 g_doomedLock;
		std::vector<std::uint32_t> g_doomed;

		void SetDoomed(std::uint32_t handle, bool doomed)
		{
			std::scoped_lock lock(g_doomedLock);
			const auto       it = std::ranges::find(g_doomed, handle);
			if (doomed && it == g_doomed.end()) {
				g_doomed.push_back(handle);
			} else if (!doomed && it != g_doomed.end()) {
				g_doomed.erase(it);
			}
		}

		// The strike animation variants ([Animation] sEvent).
		struct Variant
		{
			std::string event;
			float       pitch{ 0.0f };  // radians added to every strike effect
			float       roll{ 0.0f };
		};
		std::vector<Variant> g_variants;
		Variant              g_currentVariant{};
		RE::BGSSoundDescriptorForm* g_sheatheSound{ nullptr };

		RE::TESEffectShader* g_vanishShader{ nullptr };
		RE::TESEffectShader* g_appearShader{ nullptr };
		RE::BGSArtObject*    g_vanishArt{ nullptr };
		RE::BGSArtObject*    g_appearArt{ nullptr };
		ReactiveDodgeAPI::RequestDodge_t g_rdfDodge{ nullptr };

		RE::TESImageSpaceModifier*          g_flash{ nullptr };
		RE::BGSSoundDescriptorForm*         g_strikeSound{ nullptr };
		RE::BGSSoundDescriptorForm*         g_slowSound{ nullptr };
		std::vector<RE::BGSImpactDataSet*>  g_strikeImpacts;
		std::vector<RE::BGSArtObject*>      g_strikeArt;

		struct Vfx
		{
			std::string model;
			float       scale{ 1.0f };
			float       roll{ 0.0f };   // radians, about the swing's forward axis
			float       pitch{ 0.0f };  // radians, about its side axis (lays a slash facing you flat)
		};
		std::vector<Vfx> g_strikeVfx;
		bool                        g_dfBound{ false };

		std::mt19937 g_rng{ std::random_device{}() };

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

		RE::Actor* ActorOf(std::uint32_t handle)
		{
			const auto ref = RE::TESObjectREFR::LookupByHandle(handle);
			return ref ? ref->As<RE::Actor>() : nullptr;
		}

		std::vector<std::string> SplitList(const std::string& text)
		{
			std::vector<std::string> out;
			std::size_t              at = 0;
			while (at <= text.size()) {
				const auto comma = text.find(',', at);
				auto       item = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
				const auto b = item.find_first_not_of(" \t");
				const auto e = item.find_last_not_of(" \t");
				if (b != std::string::npos) {
					out.push_back(item.substr(b, e - b + 1));
				}
				if (comma == std::string::npos) {
					break;
				}
				at = comma + 1;
			}
			return out;
		}

		// "Plugin.esp|0xFormID" (FormID local to the plugin); null when the plugin is not loaded or the type differs.
		template <class T>
		T* LookupForm(const std::string& spec, const char* what)
		{
			const auto bar = spec.find('|');
			auto*      data = RE::TESDataHandler::GetSingleton();
			if (bar == std::string::npos || !data) {
				logger::warn("{} '{}': expected Plugin.esp|0xFormID", what, spec);
				return nullptr;
			}
			const auto id = static_cast<RE::FormID>(std::strtoul(spec.c_str() + bar + 1, nullptr, 0));
			return data->LookupForm<T>(id, spec.substr(0, bar));
		}

		// The first form of a list that resolves.
		template <class T>
		T* LookupFirst(const std::string& list, const char* what)
		{
			const auto specs = SplitList(list);
			for (const auto& spec : specs) {
				if (auto* form = LookupForm<T>(spec, what)) {
					logger::info("{}: {}", what, spec);
					return form;
				}
			}
			if (!specs.empty()) {
				logger::warn("{} '{}': none found (plugin not loaded, or not of the expected type)", what, list);
			}
			return nullptr;
		}

		// Every form of a list that resolves.
		template <class T>
		std::vector<T*> LookupAll(const std::string& list, const char* what)
		{
			std::vector<T*> out;
			for (const auto& spec : SplitList(list)) {
				if (auto* form = LookupForm<T>(spec, what)) {
					out.push_back(form);
				} else {
					logger::info("{} '{}': not found, skipped", what, spec);
				}
			}
			return out;
		}

		// Art objects for bare .nif effects (no form for them exists in any plugin).
		// "path scale rollDegrees pitchDegrees" (all but the path optional).
		std::vector<Vfx> ParseVfx(const std::string& list)
		{
			std::vector<Vfx> out;
			for (const auto& item : SplitList(list)) {
				std::istringstream in(item);
				Vfx                v;
				float              rollDeg = 0.0f;
				float              pitchDeg = 0.0f;
				in >> v.model;
				if (!(in >> v.scale)) {
					v.scale = 1.0f;
				}
				in >> rollDeg >> pitchDeg;
				v.roll = rollDeg * std::numbers::pi_v<float> / 180.0f;
				v.pitch = pitchDeg * std::numbers::pi_v<float> / 180.0f;
				if (!v.model.empty()) {
					out.push_back(std::move(v));
				}
			}
			return out;
		}

		std::vector<RE::BGSArtObject*> MakeArt(const std::string& list)
		{
			std::vector<RE::BGSArtObject*> out;
			auto*                          factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSArtObject>();
			for (const auto& model : SplitList(list)) {
				auto* art = factory ? factory->Create() : nullptr;
				if (!art) {
					logger::warn("[Effects] sStrikeArt: could not create an art object");
					break;
				}
				art->SetModel(model.c_str());
				art->data.artType = RE::BGSArtObject::ArtType::kMagicHitEffect;
				out.push_back(art);
			}
			return out;
		}

		// ---- slow motion through the engine's magic slowdown (as Reactive Dodge Framework / the Slow Time shout)
		namespace slow
		{
			using Clock = std::chrono::steady_clock;

			void Start();

			bool              g_active{ false };
			Clock::time_point g_start{};
			float             g_lastWorld{ 0.0f };
			Clock::time_point g_hitstopUntil{};
			float             g_k{ 0.0f };  // the envelope written last (0 normal .. 1 full slow motion)

			float Smooth(float x)
			{
				x = std::clamp(x, 0.0f, 1.0f);
				return x * x * (3.0f - 2.0f * x);
			}

			float Envelope(float t, const Settings& s)
			{
				if (t < s.slowEaseIn) {
					return Smooth(t / s.slowEaseIn);
				}
				t -= s.slowEaseIn;
				if (t < s.slowHold) {
					return 1.0f;
				}
				t -= s.slowHold;
				return s.slowEaseOut > 0.0f ? 1.0f - Smooth(t / s.slowEaseOut) : 0.0f;
			}

			void Write(RE::VATS* vats, float k)
			{
				const auto& s = Settings::Get();
				g_k = k;
				if (Clock::now() < g_hitstopUntil) {  // the freeze on the strike: everyone, the player too
					vats->SetMagicTimeSlowdown(s.hitstopScale, s.hitstopScale);
					g_lastWorld = s.hitstopScale;
					return;
				}
				const float world = std::lerp(1.0f, s.slowWorld, k);
				vats->SetMagicTimeSlowdown(world, std::lerp(1.0f, s.slowPlayer, k));
				g_lastWorld = world;
			}

			void End(RE::VATS* vats)
			{
				if (vats && vats->magicTimeSlowdown == g_lastWorld) {
					vats->SetMagicTimeSlowdown(0.0f, 1.0f);
				}
				g_active = false;
				g_k = 0.0f;
			}

			void Hitstop()
			{
				const auto& s = Settings::Get();
				if (s.hitstop <= 0.0f) {
					return;
				}
				g_hitstopUntil = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(s.hitstop));
				if (!g_active) {
					Start();
				}
				if (auto* vats = RE::VATS::GetSingleton(); vats && g_active) {
					Write(vats, g_k);
				}
			}

			void Start()
			{
				const auto& s = Settings::Get();
				if (g_active) {
					return;
				}
				auto*       vats = RE::VATS::GetSingleton();
				if (!vats || s.slowWorld >= 1.0f || s.slowEaseIn + s.slowHold + s.slowEaseOut <= 0.0f) {
					return;
				}
				if (vats->mode != RE::VATS::VATS_MODE::kNone) {
					return;  // a kill cam owns time
				}
				// Another slowdown (Reactive Dodge Framework's perfect dodge, the shout) is taken over: theirs notices
				// the value changed and lets go.
				const float before = vats->magicTimeSlowdown;
				g_active = true;
				g_start = Clock::now();
				Write(vats, s.slowEaseIn > 0.0f ? 0.0f : 1.0f);
				DevLog("issen: slow motion world x{:.2f} player x{:.2f}{}", s.slowWorld, s.slowPlayer,
					before > 0.0f ? std::format(" (took over a slowdown of x{:.2f})", before) : "");
			}

			// `holding`: a strike is still to land; the slowdown stays full until it has, then runs its hold and ease-out.
			void Update(bool holding)
			{
				if (!g_active) {
					return;
				}
				auto* vats = RE::VATS::GetSingleton();
				if (!vats) {
					g_active = false;
					return;
				}
				if (vats->magicTimeSlowdown != g_lastWorld) {
					if (!holding || vats->mode != RE::VATS::VATS_MODE::kNone) {
						g_active = false;  // someone else's now
						DevLog("{}", "issen: slow motion taken over by another slowdown");
						return;
					}
					// mid-issen another slowdown wrote over ours (Reactive Dodge Framework's perfect dodge ending): ours again
					DevLog("issen: slow motion reasserted (was x{:.2f})", vats->magicTimeSlowdown);
				}
				const auto& s = Settings::Get();
				if (holding) {
					const auto full = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<float>(s.slowEaseIn));
					if (Clock::now() - g_start > full) {
						g_start = Clock::now() - full;
					}
				}
				const float t = std::chrono::duration<float>(Clock::now() - g_start).count();
				if (t >= s.slowEaseIn + s.slowHold + s.slowEaseOut) {
					End(vats);
					return;
				}
				Write(vats, Envelope(t, s));
			}
		}

		// Third-person field of view narrowed during the issen (real time, so slow motion does not stretch it).
		namespace zoom
		{
			using Clock = std::chrono::steady_clock;

			bool              g_on{ false };
			bool              g_out{ false };
			float             g_base{ 0.0f };
			float             g_level{ 0.0f };  // 0..1
			Clock::time_point g_last{};

			void Update(bool issen)
			{
				const auto& s = Settings::Get();
				auto*       camera = RE::PlayerCamera::GetSingleton();
				if (!camera) {
					return;
				}
				const auto  now = Clock::now();
				const float dt = g_on ? std::chrono::duration<float>(now - g_last).count() : 0.0f;
				g_last = now;
				if (issen && s.zoomFov > 0.0f && !g_on) {
					g_on = true;
					g_base = camera->worldFOV;
					g_level = 0.0f;
				}
				if (!g_on) {
					return;
				}
				if (issen) {
					g_level = s.zoomIn > 0.0f ? std::min(1.0f, g_level + dt / s.zoomIn) : 1.0f;
				} else {
					g_level = s.zoomOut > 0.0f ? std::max(0.0f, g_level - dt / s.zoomOut) : 0.0f;
				}
				const float k = g_level * g_level * (3.0f - 2.0f * g_level);
				camera->worldFOV = g_base - s.zoomFov * k;
				if (!issen && g_level <= 0.0f) {
					camera->worldFOV = g_base;
					g_on = false;
				}
			}

			void Reset()
			{
				if (g_on) {
					if (auto* camera = RE::PlayerCamera::GetSingleton()) {
						camera->worldFOV = g_base;
					}
				}
				g_on = false;
			}
		}

		// Colour drained with the slow motion: the image space's override data, a snapshot of the current one with the
		// cinematic saturation lowered, removed again when the slow motion is over.
		namespace grade
		{
			bool                    g_on{ false };
			RE::ImageSpaceBaseData  g_data{};
			RE::ImageSpaceBaseData* g_previous{ nullptr };
			float                   g_baseSaturation{ 1.0f };

			void Update(float k)
			{
				const auto& s = Settings::Get();
				auto*       ism = RE::ImageSpaceManager::GetSingleton();
				if (!ism) {
					return;
				}
				auto& override_ = ism->GetOverrideBaseData();
				if (k > 0.001f && s.saturation < 1.0f) {
					if (!g_on) {
						const auto* current = ism->GetCurrentBaseData();
						if (!current) {
							return;
						}
						g_data = *current;
						g_baseSaturation = current->cinematic.saturation;
						g_previous = override_;
						g_on = true;
					}
					g_data.cinematic.saturation = std::lerp(g_baseSaturation, g_baseSaturation * s.saturation, k);
					override_ = &g_data;
				} else if (g_on) {
					override_ = g_previous;
					g_on = false;
				}
			}

			void Reset()
			{
				if (g_on) {
					if (auto* ism = RE::ImageSpaceManager::GetSingleton()) {
						ism->GetOverrideBaseData() = g_previous;
					}
				}
				g_on = false;
			}
		}

		void PlaySound(RE::BGSSoundDescriptorForm* sound, RE::TESObjectREFR* at)
		{
			auto* audio = RE::BSAudioManager::GetSingleton();
			if (!sound || !audio) {
				return;
			}
			RE::BSSoundHandle handle;
			if (!audio->GetSoundHandle(handle, sound)) {
				return;
			}
			if (at) {
				handle.SetPosition(at->GetPosition());
				if (auto* node = at->Get3D()) {
					handle.SetObjectToFollow(node);
				}
			}
			handle.Play();
		}

		// Orientation of an effect in the swing's frame: pitch (about the side axis) and roll (about the forward axis)
		// in the effect's own axes, then the player's heading. Built as a matrix so the result does not depend on the
		// heading (Euler angles applied in world axes tilted the slash differently for every direction faced).
		// Column vectors, column 1 = local forward in world space; heading clockwise from north.
		RE::NiMatrix3 SwingRotation(float yaw, float pitch, float roll)
		{
			RE::NiMatrix3 z, x, y;
			const float   cz = std::cos(yaw), sz = std::sin(yaw);
			z.entry[0][0] = cz;   z.entry[0][1] = sz;  z.entry[0][2] = 0.0f;
			z.entry[1][0] = -sz;  z.entry[1][1] = cz;  z.entry[1][2] = 0.0f;
			z.entry[2][0] = 0.0f; z.entry[2][1] = 0.0f; z.entry[2][2] = 1.0f;
			const float cx = std::cos(pitch), sx = std::sin(pitch);
			x.entry[0][0] = 1.0f; x.entry[0][1] = 0.0f; x.entry[0][2] = 0.0f;
			x.entry[1][0] = 0.0f; x.entry[1][1] = cx;   x.entry[1][2] = -sx;
			x.entry[2][0] = 0.0f; x.entry[2][1] = sx;   x.entry[2][2] = cx;
			const float cy = std::cos(roll), sy = std::sin(roll);
			y.entry[0][0] = cy;   y.entry[0][1] = 0.0f; y.entry[0][2] = sy;
			y.entry[1][0] = 0.0f; y.entry[1][1] = 1.0f; y.entry[1][2] = 0.0f;
			y.entry[2][0] = -sy;  y.entry[2][1] = 0.0f; y.entry[2][2] = cy;
			return z * x * y;
		}

		void PlayStrikeEffects(RE::PlayerCharacter* player, RE::Actor* target)
		{
			const auto& s = Settings::Get();
			if (auto* impacts = RE::BGSImpactManager::GetSingleton(); impacts && !g_strikeImpacts.empty()) {
				// pick from the player towards the enemy so the impact lands on its body
				auto dir = target->GetPosition() - player->GetPosition();
				dir.z = 0.0f;
				const float len = dir.Length();
				dir = len > 1.0f ? dir / len : RE::NiPoint3{ 0.0f, 0.0f, -1.0f };
				for (auto* ipds : g_strikeImpacts) {
					impacts->PlayImpactEffect(target, ipds, s.strikeImpactNode.c_str(), dir, 512.0f, false, false);
				}
			}
			for (auto* art : g_strikeArt) {
				target->ApplyArtObject(art, s.strikeArtDuration);
			}
			auto* cell = target->GetParentCell();
			if (cell && !g_strikeVfx.empty()) {
				RE::NiPoint3 at = target->GetPosition();
				at.z += 90.0f;
				if (auto* root = target->Get3D()) {
					if (auto* node = root->GetObjectByName(s.strikeImpactNode)) {
						at = node->world.translate;
					}
				}
				const float yaw = player->GetAngleZ();  // the swing goes across the player's facing
				for (const auto& v : g_strikeVfx) {
					const auto  rot = SwingRotation(yaw, v.pitch + g_currentVariant.pitch, v.roll + g_currentVariant.roll);
					const auto* fx = RE::BSTempEffectParticle::Spawn(cell, s.strikeVfxLifetime, v.model.c_str(), rot, at, v.scale, 7, nullptr);
					if (!fx) {
						DevLog("issen: effect '{}' could not be spawned", v.model);
					}
				}
			}
		}

		void FaceTarget(RE::PlayerCharacter* player, RE::Actor* target)
		{
			const float offset = player->GetHeadingAngle(target->GetPosition(), false);  // degrees, clockwise positive
			player->SetHeading(player->GetAngleZ() + offset * std::numbers::pi_v<float> / 180.0f);
		}

		float WeaponDamage(RE::PlayerCharacter* player)
		{
			const auto* form = player->GetEquippedObject(false);
			const auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
			return weapon ? static_cast<float>(weapon->GetAttackDamage()) : 0.0f;
		}

		void Dismember(RE::PlayerCharacter* player, RE::Actor* target, const rules::Rule& rule)
		{
			const auto& d = rule.outcome.dismember;
			if (!Settings::Get().dismember || !d.enabled || !g_dfBound || !DismemberingFrameworkAPI::g_API) {
				return;
			}
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			if (roll(g_rng) >= d.chance) {
				DevLog("issen: no dismemberment this time (chance {:.2f})", d.chance);
				return;
			}
			// Rules list nodes of several skeletons (humans, canines...): keep the ones this actor has.
			auto* root = target->Get3D();
			std::vector<rules::NodeWeight> nodes;
			for (const auto& n : d.nodes) {
				if (root && root->GetObjectByName(n.node)) {
					nodes.push_back(n);
				}
			}
			const auto* form = player->GetEquippedObject(false);
			auto*       weapon = form ? const_cast<RE::TESObjectWEAP*>(form->As<RE::TESObjectWEAP>()) : nullptr;
			// Dismembering Framework cuts only nodes its data packs describe for this race: try in weighted order. Success
			// shows in its limb flags (set at once); an actor already missing a limb gets a single try.
			const bool already = DismemberingFrameworkAPI::g_API->IsDismembered(target);
			while (!nodes.empty()) {
				const auto node = rules::PickNode(nodes, roll(g_rng));
				if (node.empty()) {
					break;
				}
				DismemberingFrameworkAPI::DismembermentParams params;
				params.forceExecution = true;
				params.specificNode = node;
				params.ignoreArmorClass = true;
				DismemberingFrameworkAPI::g_API->Dismember(target, RE::BSFixedString(node), player, weapon, &params);
				if (already) {
					logger::info("issen: dismember {:08X} at '{}' (it already missed a limb: one try)", target->GetFormID(), node);
					return;
				}
				if (DismemberingFrameworkAPI::g_API->IsDismembered(target)) {
					logger::info("issen: dismembered {:08X} at '{}'", target->GetFormID(), node);
					return;
				}
				DevLog("issen: Dismembering Framework did not cut '{}' on {:08X} (no data for it), trying another", node, target->GetFormID());
				std::erase_if(nodes, [&](const rules::NodeWeight& n) { return n.node == node; });
			}
			logger::info("issen: no node of the rule could be cut on {:08X} ({} of {} exist on its skeleton)", target->GetFormID(),
				std::ranges::count_if(d.nodes, [&](const rules::NodeWeight& n) { return root && root->GetObjectByName(n.node); }), d.nodes.size());
		}

		// The issen deals its damage directly, which raises no hit event of its own: send one, so Papyrus OnHit (and any
		// plugin listening for hits) sees the strike like a normal hit by the weapon in hand.
		void SendHitEvent(RE::PlayerCharacter* player, RE::Actor* target)
		{
			const auto& s = Settings::Get();
			auto*       events = RE::ScriptEventSourceHolder::GetSingleton();
			if (!s.sendHitEvent || !events) {
				return;
			}
			constexpr RE::FormID kUnarmed = 0x000001F4;  // Skyrim.esm "Unarmed"
			const auto*          form = player->GetEquippedObject(false);
			const auto*          weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
			const auto           flags = s.hitPowerAttack ? RE::TESHitEvent::Flag::kPowerAttack : RE::TESHitEvent::Flag::kNone;
			const RE::TESHitEvent event{ target, player, weapon ? weapon->GetFormID() : kUnarmed, 0, flags };
			events->SendEvent(&event);
			DevLog("issen: hit event sent for {:08X} (source {:08X}{})", target->GetFormID(), event.source,
				s.hitPowerAttack ? ", power attack" : "");
		}

		// What the engine casts on a real weapon hit and the direct damage skips: the perks' "Apply Combat Hit Spell" (the
		// axes' bleeding and the like) and the weapon's enchantment (its charge is not drained).
		void CastHitSpells(RE::PlayerCharacter* player, RE::Actor* target)
		{
			if (!Settings::Get().hitSpells) {
				return;
			}
			auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!caster) {
				return;
			}
			const auto* form = player->GetEquippedObject(false);
			auto*       weapon = form ? const_cast<RE::TESObjectWEAP*>(form->As<RE::TESObjectWEAP>()) : nullptr;
			if (!weapon) {
				weapon = RE::TESForm::LookupByID<RE::TESObjectWEAP>(0x000001F4);  // Skyrim.esm "Unarmed"
			}
			RE::SpellItem* perkSpell = nullptr;
			RE::BGSEntryPoint::HandleEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kApplyCombatHitSpell, player, weapon, target, &perkSpell);
			if (perkSpell) {
				caster->CastSpellImmediate(perkSpell, false, target, 1.0f, false, 0.0f, player);
			}
			RE::EnchantmentItem* enchantment = nullptr;
			if (const auto* entry = player->GetEquippedEntryData(false)) {
				const auto charge = entry->GetEnchantmentCharge();
				if (!charge || *charge > 0.0) {
					enchantment = entry->GetEnchantment();
				}
			}
			if (enchantment) {
				caster->CastSpellImmediate(enchantment, false, target, 1.0f, false, 0.0f, player);
			}
			DevLog("issen: hit spells on {:08X}: perk '{}', enchantment '{}'", target->GetFormID(), perkSpell ? perkSpell->GetName() : "-",
				enchantment ? enchantment->GetName() : "-");
		}

		// The strike lands as a real weapon hit (bRealHit): the game applies what its hits carry and sends OnHit; `damage`
		// is the least the target loses (the game's difficulty and the target's perks scale the hit down, never below it).
		// True when the game took the hit; false: nothing was applied but the hit event and the hit spells.
		bool LandHit(RE::PlayerCharacter* player, RE::Actor* target, float damage)
		{
			if (Settings::Get().realHit) {
				auto*       av = target->AsActorValueOwner();
				const float before = av->GetActorValue(RE::ActorValue::kHealth);
				if (hooks::RealHit(player, target, damage)) {
					const float taken = before - av->GetActorValue(RE::ActorValue::kHealth);
					const bool  topUp = damage > 0.0f && taken < damage - 0.5f && !target->IsDead();
					if (topUp) {
						av->DamageActorValue(RE::ActorValue::kHealth, damage - taken);
					}
					DevLog("issen: real hit on {:08X}: {:.0f} asked, the game took {:.0f}{}", target->GetFormID(), damage, taken,
						topUp ? ", the rest added" : "");
					return true;
				}
				logger::info("issen: the game dropped the real hit on {:08X}: direct damage instead", target->GetFormID());
			}
			SendHitEvent(player, target);
			CastHitSpells(player, target);
			return false;
		}

		bool TryKill(RE::PlayerCharacter* player, RE::Actor* target)
		{
			auto*       av = target->AsActorValueOwner();
			const float health = av->GetActorValue(RE::ActorValue::kHealth);
			target->KillImpl(player, std::max(health, 1.0f), true, false);
			if (!target->IsDead()) {
				av->DamageActorValue(RE::ActorValue::kHealth, health + 10.0f);
			}
			return target->IsDead();
		}

		// The delayed deaths: the sheathe, the kill, then (once the engine shows the actor dead, which can take a few
		// frames) the dismemberment. An actor that does not die gets one more try, then is let go with a log line.
		void UpdateDeaths(RE::PlayerCharacter* player, float dt)
		{
			for (auto it = g_deaths.begin(); it != g_deaths.end();) {
				auto& d = *it;
				auto* target = ActorOf(d.target);
				if (!target || !target->Is3DLoaded()) {
					SetDoomed(d.target, false);
					it = g_deaths.erase(it);
					continue;
				}
				d.timer -= dt;
				if (d.timer > 0.0f) {
					++it;
					continue;
				}
				if (!d.killed) {
					d.killed = true;
					d.verifyLeft = Settings::Get().killVerifyTime;
					++d.tries;
					TryKill(player, target);
				}
				if (target->IsDead()) {
					g_sheatheAfterCombat = Settings::Get().sheathe;
					logger::info("issen: {:08X} died (rule '{}'){}", target->GetFormID(), d.rule->name, d.tries > 1 ? " on the second try" : "");
					Dismember(player, target, *d.rule);
					SetDoomed(d.target, false);
					it = g_deaths.erase(it);
					continue;
				}
				d.verifyLeft -= dt;
				if (d.verifyLeft > 0.0f) {
					++it;
					continue;
				}
				if (d.tries < 2) {
					d.killed = false;  // once more next tick
					++it;
					continue;
				}
				auto* av = target->AsActorValueOwner();
				logger::info("issen: {:08X} NOT killed after {} tries (health {:.0f}, essential {}, protected {}, in kill move {}, race {})",
					target->GetFormID(), d.tries, av->GetActorValue(RE::ActorValue::kHealth), target->IsEssential(), target->IsProtected(),
					target->IsInKillMove(), target->GetRace() ? target->GetRace()->GetFormEditorID() : "?");
				SetDoomed(d.target, false);
				it = g_deaths.erase(it);
			}
		}

		void Strike(RE::PlayerCharacter* player, RE::Actor* target, const rules::Rule& rule)
		{
			auto*       av = target->AsActorValueOwner();
			const float health = av->GetActorValue(RE::ActorValue::kHealth);
			const float maxHealth = av->GetPermanentActorValue(RE::ActorValue::kHealth);
			const auto& set = Settings::Get();
			const bool  boss = enemy::IsBoss(target);
			const auto  outcome = set.damageGlobal ? rules::WithGlobalDamage(rule.outcome, set.damage, boss) : rule.outcome;
			const auto  verdict = rules::Resolve(outcome, health, maxHealth, WeaponDamage(player));
			DevLog("issen: damage from {}{}", set.damageGlobal ? "the menu's global settings" : "the rule", boss ? " (boss)" : "");

			PlaySound(g_strikeSound, target);
			PlayStrikeEffects(player, target);
			slow::Hitstop();
			const auto& s = Settings::Get();
			if (verdict.kill && !target->IsEssential() && s.deathDelayed) {
				// Onimusha: it stands while you sheathe, then falls. Its hits no longer land meanwhile.
				LandHit(player, target, 0.0f);  // the hit's effects now, the death later
				const auto handle = HandleOf(target);
				g_deaths.push_back({ handle, &rule, s.deathDelay, -1.0f });
				SetDoomed(handle, true);
				logger::info("issen: {:08X} struck, dies in {:.2f}s (rule '{}', health {:.0f}/{:.0f})", target->GetFormID(), s.deathDelay, rule.name,
					health, maxHealth);
				return;
			}
			if (verdict.kill) {
				if (target->IsEssential()) {
					// An essential actor only goes down to its knees; cutting it there leaves it kneeling without a head.
					if (!LandHit(player, target, health + 10.0f)) {
						av->DamageActorValue(RE::ActorValue::kHealth, health + 10.0f);
					}
					logger::info("issen: {:08X} is essential: knocked down instead of killed, no dismemberment (rule '{}')", target->GetFormID(),
						rule.name);
					return;
				}
				LandHit(player, target, health + 10.0f);
				if (!target->IsDead()) {
					target->KillImpl(player, std::max(health, 1.0f), true, false);
				}
				if (!target->IsDead()) {
					av->DamageActorValue(RE::ActorValue::kHealth, health + 10.0f);  // KillImpl refused: take the health instead
				}
				const bool dead = target->IsDead();
				logger::info("issen: {:08X} {} (rule '{}', health {:.0f}/{:.0f}{})", target->GetFormID(), dead ? "killed" : "NOT killed", rule.name,
					health, maxHealth, target->IsProtected() ? ", protected" : "");
				if (dead) {
					Dismember(player, target, rule);
				} else {
					logger::info("issen: {:08X} still alive: no dismemberment (it would stand without the limb)", target->GetFormID());
				}
				return;
			}
			if (!LandHit(player, target, verdict.damage)) {
				av->DamageActorValue(RE::ActorValue::kHealth, verdict.damage);
			}
			if (verdict.stagger > 0.0f) {
				target->SetGraphVariableFloat("staggerMagnitude", verdict.stagger);
				target->NotifyAnimationGraph("staggerStart");
			}
			logger::info("issen: {:08X} takes {:.0f} (rule '{}', health {:.0f} -> {:.0f} of {:.0f}){}", target->GetFormID(), verdict.damage,
				rule.name, health, health - verdict.damage, maxHealth, verdict.stagger > 0.0f ? ", staggered" : "");
		}

		// Right in front of the enemy, on the side the player is on; the player's own spot when already that close.
		RE::NiPoint3 DashEnd(RE::PlayerCharacter* player, RE::Actor* target)
		{
			const auto&  s = Settings::Get();
			RE::NiPoint3 away = player->GetPosition() - target->GetPosition();
			away.z = 0.0f;
			const float len = away.Length();
			if (len <= s.dashStopDistance + 1.0f) {
				return player->GetPosition();
			}
			RE::NiPoint3 to = target->GetPosition() + away / len * s.dashStopDistance;
			to.z = target->GetPosition().z;
			return to;
		}

		float NormalWeaponSpeed(RE::PlayerCharacter* player)
		{
			const float v = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWeaponSpeedMult);
			return v > 0.0f ? v : 1.0f;
		}

		void TakeSpeedOff()
		{
			if (g_speedDelta == 0.0f) {
				return;
			}
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				player->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kWeaponSpeedMult, -g_speedDelta);
			}
			g_speedDelta = 0.0f;
		}

		// The issen strike clip: stop what is playing, start the configured attack, sped up.
		bool PlayStrikeAnimation(RE::PlayerCharacter* player, float speed)
		{
			const auto& s = Settings::Get();
			if (g_variants.empty()) {
				g_currentVariant = {};
				return false;
			}
			g_currentVariant = g_variants[std::uniform_int_distribution<std::size_t>(0, g_variants.size() - 1)(g_rng)];
			// re-registered each time: the graph is rebuilt on load and on a camera switch
			player->RemoveAnimationGraphEventSink(&g_graphSink);
			player->AddAnimationGraphEventSink(&g_graphSink);
			g_hitFramesAtBegin = g_hitFrames.load(std::memory_order_relaxed);

			// The attack reads WeaponSpeedMult as it starts (the graph variable alone is overwritten every frame).
			RestoreSpeed();
			auto*       av = player->AsActorValueOwner();
			const float current = av->GetActorValue(RE::ActorValue::kWeaponSpeedMult);
			const float normal = current > 0.0f ? current : 1.0f;  // 0 counts as 1
			g_speedDelta = normal * speed - current;
			av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kWeaponSpeedMult, g_speedDelta);
			player->SetGraphVariableFloat("weaponSpeedMult", normal * speed);
			const auto* actorState = player->AsActorState();
			const bool  attacking = actorState && actorState->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
			const bool  stopped = s.animStopFirst && attacking && player->NotifyAnimationGraph("attackStop");
			const bool started = player->NotifyAnimationGraph(RE::BSFixedString(g_currentVariant.event));
			player->SetGraphVariableFloat("weaponSpeedMult", normal * speed);
			g_speedLeft = s.animSpeedTime;
			g_speedRestore = true;
			DevLog("issen: animation '{}' {} (attackStop {}), speed x{:.1f}", g_currentVariant.event, started ? "started" : "REJECTED by the graph",
				!s.animStopFirst || !attacking ? "not sent" : (stopped ? "taken" : "rejected"), speed);
			return started;
		}

		void PlayOn(RE::PlayerCharacter* player, RE::TESEffectShader* shader, RE::BGSArtObject* art)
		{
			const float t = Settings::Get().approachFxTime;
			if (shader && t > 0.0f) {
				player->ApplyEffectShader(shader, t);
			}
			if (art && t > 0.0f) {
				player->ApplyArtObject(art, t);
			}
		}

		void StartStrike(RE::PlayerCharacter* player, RE::Actor* target, std::uint32_t handle, const rules::Rule* rule, bool afterDash = false)
		{
			const auto& s = Settings::Get();
			if (s.faceTarget) {
				FaceTarget(player, target);
			}
			const bool animated = PlayStrikeAnimation(player, afterDash ? s.animSpeedAfterDash : s.animSpeed);
			if (s.devLog) {
				g_graphSink.tracing.store(true, std::memory_order_relaxed);
				g_traceLeft = 1.0f;
			}
			const bool  onHitFrame = animated && s.strikeOnHitFrame;
			const float delay = onHitFrame ? s.strikeMaxDelay : s.strikeDelay;
			g_strikes.push_back({ handle, rule, delay, onHitFrame });
			if (s.lockPosition) {
				g_lock = true;
				g_lockAt = player->GetPosition();
				g_lockAfter = s.lockAfter;
			}
			DevLog("issen: strike against {:08X} {} {:.2f}s", target->GetFormID(), onHitFrame ? "on HitFrame, at most in" : "in", delay);
		}

		// The dash: a fast move to the enemy over [Approach] fVanishTime, silhouettes left along it...
		void Vanish(RE::PlayerCharacter* player, RE::Actor* target, Approach& a)
		{
			PlayOn(player, g_vanishShader, g_vanishArt);
			a.phase = Phase::Vanish;
			a.timer = Settings::Get().vanishTime;
			a.from = player->GetPosition();
			a.to = a.dash ? DashEnd(player, target) : a.from;
			FaceTarget(player, target);
			ghost::Spawn(player);
			DevLog("issen: dash {:.0f} units to {:08X}", a.from.GetDistance(a.to), target->GetFormID());
		}

		void DashStep(RE::PlayerCharacter* player, const Approach& a)
		{
			const float d = Settings::Get().vanishTime;
			const float t = d > 0.0f ? std::clamp(1.0f - a.timer / d, 0.0f, 1.0f) : 1.0f;
			player->SetPosition(a.from + (a.to - a.from) * t, true);
		}

		// ...and there, in front of the enemy: a moment's pause ([Approach] fAppearPause), then the strike.
		void Appear(RE::PlayerCharacter* player, RE::Actor* target, Approach& a)
		{
			if (a.dash) {
				player->SetPosition(a.to, true);
			}
			FaceTarget(player, target);
			PlayOn(player, g_appearShader, g_appearArt);
			a.phase = Phase::Arrived;
			a.timer = Settings::Get().appearPause;
		}

		void Begin(RE::PlayerCharacter* player, RE::Actor* target, const Request& r)
		{
			const auto& s = Settings::Get();
			if (s.faceTarget) {
				FaceTarget(player, target);
			}
			slow::Start();
			if (g_flash && s.flashStrength > 0.0f) {
				RE::ImageSpaceModifierInstanceForm::Trigger(g_flash, s.flashStrength, nullptr);
			}
			PlaySound(g_slowSound, nullptr);

			const bool perfect = r.kind == rules::Kind::PerfectDodge;
			const bool preStep = !perfect && s.preStep;
			Approach   a{ r.target, r.rule, r.kind, Phase::PreStep, 0.0f, perfect ? s.dashOnPerfect : preStep };
			if (a.dash) {
				ghost::Prepare(player);
			}
			DevLog("issen: begin against {:08X} ({}){}", target->GetFormID(), perfect ? "perfect dodge" : "normal",
				preStep ? ", back step first" : (a.dash ? ", dash" : ""));
			if (preStep) {
				if (g_rdfDodge && g_rdfDodge(player, s.preStepAngle)) {
					a.timer = s.preStepTime;
					g_approaches.push_back(a);
					return;
				}
				DevLog("{}", "issen: no back step (Reactive Dodge Framework not loaded or the request was refused)");
			}
			if (a.dash) {
				Vanish(player, target, a);
				g_approaches.push_back(a);
				return;
			}
			StartStrike(player, target, r.target, r.rule);
		}
	}

	void Init()
	{
		const auto& s = Settings::Get();
		g_flash = LookupFirst<RE::TESImageSpaceModifier>(s.flashImod, "[Effects] sFlashImod");
		g_strikeSound = LookupFirst<RE::BGSSoundDescriptorForm>(s.strikeSound, "[Effects] sStrikeSound");
		g_slowSound = LookupFirst<RE::BGSSoundDescriptorForm>(s.slowSound, "[Effects] sSlowSound");
		g_strikeImpacts = LookupAll<RE::BGSImpactDataSet>(s.strikeImpacts, "[Effects] sStrikeImpacts");
		g_strikeArt = MakeArt(s.strikeArt);
		g_strikeVfx = ParseVfx(s.strikeVfx);
		g_vanishShader = s.vanishShader.empty() ? nullptr : LookupFirst<RE::TESEffectShader>(s.vanishShader, "[Approach] sVanishShader");
		g_appearShader = s.appearShader.empty() ? nullptr : LookupFirst<RE::TESEffectShader>(s.appearShader, "[Approach] sAppearShader");
		g_vanishArt = s.vanishArt.empty() ? nullptr : LookupFirst<RE::BGSArtObject>(s.vanishArt, "[Approach] sVanishArt");
		g_appearArt = s.appearArt.empty() ? nullptr : LookupFirst<RE::BGSArtObject>(s.appearArt, "[Approach] sAppearArt");
		g_variants.clear();
		for (const auto& item : SplitList(s.animEvent)) {
			std::istringstream in(item);
			Variant            v;
			float              pitchDeg = 0.0f, rollDeg = 0.0f;
			in >> v.event >> pitchDeg >> rollDeg;
			v.pitch = pitchDeg * std::numbers::pi_v<float> / 180.0f;
			v.roll = rollDeg * std::numbers::pi_v<float> / 180.0f;
			if (!v.event.empty()) {
				g_variants.push_back(std::move(v));
			}
		}
		g_sheatheSound = s.sheatheSound.empty() ? nullptr : LookupFirst<RE::BGSSoundDescriptorForm>(s.sheatheSound, "[Death] sSheatheSound");
		logger::info("animation: {} strike variant(s)", g_variants.size());
		g_rdfDodge = ReactiveDodgeAPI::Get();
		ghost::Init();
		logger::info("approach: back step {}, vanish shader {}, appear shader {}, vanish art {}, appear art {}",
			g_rdfDodge ? "through Reactive Dodge Framework" : "off (Reactive Dodge Framework not loaded)", g_vanishShader ? "ok" : "off",
			g_appearShader ? "ok" : "off", g_vanishArt ? "ok" : "off", g_appearArt ? "ok" : "off");
		logger::info("effects: flash {}, strike sound {}, slow sound {}, {} impact set(s), {} art effect(s), {} world effect(s)", g_flash ? "ok" : "off",
			g_strikeSound ? "ok" : "off", g_slowSound ? "ok" : "off", g_strikeImpacts.size(), g_strikeArt.size(), g_strikeVfx.size());
	}

	void BindDismembering()
	{
		if (g_dfBound) {
			return;
		}
		if (DismemberingFrameworkAPI::LoadAPI()) {
			g_dfBound = true;
			logger::info("Dismembering Framework API bound");
		} else {
			logger::info("Dismembering Framework not found (or another API version): issen kills without dismemberment");
		}
	}

	void Trigger(RE::Actor* target, rules::Kind kind, const rules::Rule* rule)
	{
		if (!target || !rule) {
			return;
		}
		std::scoped_lock lock(g_requestLock);
		g_requests.push_back({ HandleOf(target), kind, rule });
	}

	void Update(float dt)
	{
		const bool issen = !g_strikes.empty() || !g_approaches.empty() || !g_deaths.empty();
		slow::Update(issen);
		zoom::Update(issen);
		grade::Update(slow::g_active ? slow::g_k : 0.0f);
		ghost::Update(dt);

		std::vector<Request> requests;
		{
			std::scoped_lock lock(g_requestLock);
			requests.swap(g_requests);
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		for (const auto& r : requests) {
			auto* target = ActorOf(r.target);
			if (target && !target->IsDead() && target->Is3DLoaded()) {
				Begin(player, target, r);
			}
		}

		for (auto it = g_approaches.begin(); it != g_approaches.end();) {
			it->timer -= dt;
			auto* target = ActorOf(it->target);
			if (!target || target->IsDead() || !target->Is3DLoaded()) {
				it = g_approaches.erase(it);
				continue;
			}
			if (it->timer > 0.0f) {
				if (it->phase == Phase::Vanish && it->dash) {
					DashStep(player, *it);
				} else if (it->phase == Phase::Arrived && it->dash) {
					player->SetPosition(it->to, true);  // stays where it landed
				}
				++it;
				continue;
			}
			if (it->phase == Phase::Vanish) {
				Appear(player, target, *it);
				if (it->timer > 0.0f) {
					++it;
					continue;
				}
			}
			if (it->phase == Phase::PreStep && it->dash) {
				Vanish(player, target, *it);
				++it;
				continue;
			}
			StartStrike(player, target, it->target, it->rule, it->phase == Phase::Arrived && it->dash);
			it = g_approaches.erase(it);
		}

		if (g_speedLeft > 0.0f) {
			g_speedLeft -= dt;
		}
		if (g_traceLeft > 0.0f) {
			g_traceLeft -= dt;
			if (g_traceLeft <= 0.0f) {
				g_graphSink.tracing.store(false, std::memory_order_relaxed);
			}
		}

		const bool hitFrame = g_hitFrames.load(std::memory_order_relaxed) != g_hitFramesAtBegin;
		for (auto it = g_strikes.begin(); it != g_strikes.end();) {
			it->delay -= dt;
			const bool due = it->delay <= 0.0f || (it->onHitFrame && hitFrame);
			if (!due) {
				++it;
				continue;
			}
			DevLog("issen: strike {}", it->onHitFrame && hitFrame ? "on HitFrame" : "on the timer");
			auto* target = ActorOf(it->target);
			if (target && !target->IsDead() && target->Is3DLoaded()) {
				Strike(player, target, *it->rule);
			}
			it = g_strikes.erase(it);
		}
		UpdateDeaths(player, dt);

		if (g_lock && g_strikes.empty()) {
			g_lockAfter -= dt;
			if (g_lockAfter <= 0.0f) {
				g_lock = false;
			}
		}

		if (g_sheatheAfterCombat && g_deaths.empty()) {
			g_outOfCombat = player->IsInCombat() ? 0.0f : g_outOfCombat + dt;
			if (g_outOfCombat >= Settings::Get().sheatheDelay) {
				g_sheatheAfterCombat = false;
				g_outOfCombat = 0.0f;
				if (const auto* state = player->AsActorState(); state && state->IsWeaponDrawn()) {
					player->DrawWeaponMagicHands(false);
					PlaySound(g_sheatheSound, player);
					DevLog("{}", "issen: combat over, weapon sheathed");
				}
			}
		}
	}

	bool IsDoomed(RE::Actor* actor)
	{
		if (!actor) {
			return false;
		}
		const auto       handle = actor->GetHandle().native_handle();
		std::scoped_lock lock(g_doomedLock);
		return std::ranges::find(g_doomed, handle) != g_doomed.end();
	}

	void PostUpdate()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}
		if (g_lock) {
			const auto at = player->GetPosition();
			if (at.GetDistance(g_lockAt) > 1.0f) {
				player->SetPosition(g_lockAt, true);
			}
		}
		if (g_speedLeft <= 0.0f && g_speedRestore) {
			g_speedRestore = false;
			TakeSpeedOff();
			player->SetGraphVariableFloat("weaponSpeedMult", NormalWeaponSpeed(player));
		}
	}

	void RestoreSpeed()
	{
		TakeSpeedOff();
		g_speedRestore = false;
		g_speedLeft = 0.0f;
	}

	void Reset()
	{
		g_speedLeft = 0.0f;
		g_speedRestore = false;
		g_speedDelta = 0.0f;  // a loaded game has its own values
		{
			std::scoped_lock lock(g_requestLock);
			g_requests.clear();
		}
		g_strikes.clear();
		g_approaches.clear();
		g_deaths.clear();
		g_sheatheAfterCombat = false;
		g_outOfCombat = 0.0f;
		g_lock = false;
		{
			std::scoped_lock lock(g_doomedLock);
			g_doomed.clear();
		}
		zoom::Reset();
		grade::Reset();
		ghost::Reset();
		if (slow::g_active) {
			slow::End(RE::VATS::GetSingleton());
		}
	}
}
