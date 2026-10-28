#include "Ghost.h"

#include "Settings.h"

#include "third_party/ghosttrails/GhostTrailsAPI.h"

#include <algorithm>
#include <vector>

namespace issen::game::ghost
{
	namespace
	{
		namespace gt = GhostTrailsAPI;

		gt::IVGhostTrails1* g_api = nullptr;

		// Requests this plugin owns (as Reactive Dodge Framework's perfect-dodge ghost): a spawn is released once it is
		// over and its last silhouette had its whole lifetime, a prepare at once when it is over.
		struct Owned
		{
			gt::RequestID id{ 0 };
			bool          prepare{ false };
			float         linger{ 0.0f };
			bool          ended{ false };
		};
		std::vector<Owned>    g_owned;
		constexpr std::size_t kMaxOwned = 16;

		bool Terminal(gt::State s)
		{
			return s == gt::State::Completed || s == gt::State::Partial || s == gt::State::Failed || s == gt::State::Canceled;
		}

		void Own(gt::RequestID id, bool prepare, float linger)
		{
			if (g_owned.size() >= kMaxOwned) {
				g_api->Release(g_owned.front().id);
				g_owned.erase(g_owned.begin());
			}
			g_owned.push_back({ id, prepare, linger, false });
		}

		bool Wanted(RE::Actor* actor)
		{
			return g_api && actor && Settings::Get().ghost;
		}
	}

	void Init()
	{
		g_api = gt::RequestPluginAPI();
		logger::info("dash ghosts: GhostTrails API v1 {}", g_api ? "found" : "not found (no silhouettes)");
	}

	void Prepare(RE::Actor* actor)
	{
		if (!Wanted(actor)) {
			return;
		}
		gt::RequestID id{};
		if (g_api->Prepare(actor->GetHandle().native_handle(), static_cast<std::uint32_t>(std::max(Settings::Get().ghostCount, 1)), &id) ==
			gt::Result::Accepted) {
			Own(id, true, 0.0f);
		}
	}

	void Spawn(RE::Actor* actor)
	{
		if (!Wanted(actor)) {
			return;
		}
		const auto&      s = Settings::Get();
		gt::SpawnOptions o;
		o.appearance = { s.ghostColor[0], s.ghostColor[1], s.ghostColor[2], s.ghostColor[3], s.ghostIntensity, s.ghostRimPower };
		o.count = static_cast<std::uint32_t>(std::max(s.ghostCount, 1));
		o.interval = s.ghostInterval;
		o.lifetime = s.ghostLifetime;
		o.retryWindow = 0.05f;  // a late silhouette would not stand on the dash's path
		o.spawnWindow = 0.0f;
		gt::RequestID id{};
		if (g_api->Spawn(actor->GetHandle().native_handle(), &o, &id) == gt::Result::Accepted) {
			Own(id, false, s.ghostLifetime + 0.1f);
		} else if (s.devLog) {
			logger::info("{}", "dash ghosts: the request was refused");
		}
	}

	void Update(float dt)
	{
		if (!g_api || g_owned.empty()) {
			return;
		}
		std::erase_if(g_owned, [&](Owned& o) {
			if (!o.ended) {
				gt::Status status;
				if (g_api->GetStatus(o.id, &status) != gt::Result::Accepted) {
					return true;
				}
				o.ended = Terminal(status.state);
				if (!o.ended) {
					return false;
				}
			}
			o.linger -= dt;
			if (o.prepare || o.linger <= 0.0f) {
				g_api->Release(o.id);
				return true;
			}
			return false;
		});
	}

	void Reset()
	{
		g_owned.clear();
	}
}
