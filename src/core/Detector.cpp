#include "Detector.h"

#include <algorithm>

namespace issen
{
	namespace
	{
		constexpr std::size_t kMaxPresses = 16;
		constexpr std::size_t kMaxImpacts = 32;
	}

	PressVerdict Detector::OnPress(double t, Hand hand)
	{
		PressVerdict verdict;
		const bool   mashed = !presses_.empty() && t - presses_.back().t < config_.mashGuard;

		// A perfect dodge's window takes the press first: the newest open one.
		for (auto it = perfects_.rbegin(); it != perfects_.rend(); ++it) {
			const double since = t - it->t;
			if (since >= 0.0 && since <= config_.perfectWindow) {
				if (mashed) {
					verdict.mashed = true;  // the window stays open for a clean press
					presses_.push_back({ t, hand, true });
					if (presses_.size() > kMaxPresses) {
						presses_.pop_front();
					}
					return verdict;
				}
				verdict.kind = PressVerdict::Kind::PerfectDodgeIssen;
				verdict.delta = static_cast<float>(since);
				verdict.attacker = it->attacker;
				perfects_.erase(std::next(it).base());
				presses_.push_back({ t, hand, true });
				if (presses_.size() > kMaxPresses) {
					presses_.pop_front();
				}
				return verdict;
			}
		}

		presses_.push_back({ t, hand, false });
		if (presses_.size() > kMaxPresses) {
			presses_.pop_front();
		}

		// Calibration: the newest hit this press came too late for.
		for (auto it = impacts_.rbegin(); it != impacts_.rend(); ++it) {
			const double since = t - it->t;
			if (since < 0.0) {
				continue;
			}
			if (since > config_.lateLog) {
				break;
			}
			if (!it->issen && !it->lateReported) {
				it->lateReported = true;
				verdict.kind = PressVerdict::Kind::Late;
				verdict.delta = static_cast<float>(since);
				verdict.attacker = it->attacker;
			}
			break;
		}
		return verdict;
	}

	ImpactVerdict Detector::OnImpact(double t, std::uint32_t attacker, ImpactSource, bool eligible)
	{
		ImpactVerdict verdict;

		for (auto it = impacts_.rbegin(); it != impacts_.rend(); ++it) {
			if (it->attacker == attacker && t - it->t <= config_.sameSwing) {
				verdict.duplicate = true;
				verdict.sinceFirst = static_cast<float>(t - it->t);
				verdict.firstIssen = it->issen;
				return verdict;
			}
		}

		// The newest unused press at or before the hit, and the press before it (mashing check).
		Press*       nearest = nullptr;
		const Press* before = nullptr;
		for (auto it = presses_.rbegin(); it != presses_.rend(); ++it) {
			const double lead = t - it->t;
			if (lead < 0.0) {
				continue;
			}
			if (lead > config_.earlyLog) {
				break;
			}
			if (!it->used) {
				nearest = &*it;
				if (std::next(it) != presses_.rend()) {
					before = &*std::next(it);
				}
				break;
			}
		}
		if (nearest) {
			const float lead = static_cast<float>(t - nearest->t);
			verdict.lead = lead;
			verdict.hand = nearest->hand;
			if (lead <= config_.window && before && nearest->t - before->t < config_.mashGuard) {
				verdict.mashed = true;
			} else if (eligible && lead <= config_.window) {
				verdict.issen = true;
				nearest->used = true;
			}
		}

		impacts_.push_back({ t, attacker, verdict.issen, false });
		if (impacts_.size() > kMaxImpacts) {
			impacts_.pop_front();
		}
		return verdict;
	}

	void Detector::OnPerfectDodge(double t, std::uint32_t attacker)
	{
		perfects_.push_back({ t, attacker });
	}

	void Detector::Expire(double now)
	{
		const double pressKeep = std::max(config_.earlyLog, config_.window);
		while (!presses_.empty() && now - presses_.front().t > pressKeep) {
			presses_.pop_front();
		}
		const double impactKeep = std::max(config_.lateLog, config_.sameSwing);
		while (!impacts_.empty() && now - impacts_.front().t > impactKeep) {
			impacts_.pop_front();
		}
		while (!perfects_.empty() && now - perfects_.front().t > config_.perfectWindow) {
			perfects_.pop_front();
		}
	}

	void Detector::Reset()
	{
		presses_.clear();
		impacts_.clear();
		perfects_.clear();
	}
}
