#include "check.h"

#include "core/Detector.h"

using namespace issen;

namespace
{
	constexpr std::uint32_t kBandit = 0x1001;
	constexpr std::uint32_t kDraugr = 0x1002;

	Detector Make()
	{
		Detector d;
		d.Configure({ .window = 0.12f, .earlyLog = 0.5f, .lateLog = 0.3f, .sameSwing = 0.25f, .perfectWindow = 0.4f, .mashGuard = 0.0f });
		return d;
	}
}

TEST(press_inside_window_is_issen)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	const auto v = d.OnImpact(10.1, kBandit, ImpactSource::Vanilla);
	CHECK(!v.duplicate);
	CHECK(v.issen);
	CHECK(v.lead.has_value());
	CHECK_NEAR(*v.lead, 0.1, 1e-4);
	CHECK(v.hand == Hand::Right);
}

TEST(press_on_the_hit_frame_is_issen)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Left);
	const auto v = d.OnImpact(10.0, kBandit, ImpactSource::Vanilla);
	CHECK(v.issen);
	CHECK(v.hand == Hand::Left);
}

TEST(window_edge_is_inclusive)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	CHECK(d.OnImpact(10.0 + 0.12, kBandit, ImpactSource::Vanilla).issen);
}

TEST(early_press_is_reported_not_issen)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	const auto v = d.OnImpact(10.3, kBandit, ImpactSource::Vanilla);
	CHECK(!v.issen);
	CHECK(v.lead.has_value());
	CHECK_NEAR(*v.lead, 0.3, 1e-4);
}

TEST(press_older_than_early_log_is_not_reported)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	const auto v = d.OnImpact(10.6, kBandit, ImpactSource::Vanilla);
	CHECK(!v.issen);
	CHECK(!v.lead.has_value());
}

TEST(newest_press_counts)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	d.OnPress(10.25, Hand::Left);
	const auto v = d.OnImpact(10.3, kBandit, ImpactSource::Vanilla);
	CHECK(v.issen);
	CHECK(v.hand == Hand::Left);
	CHECK_NEAR(*v.lead, 0.05, 1e-4);
}

TEST(one_press_makes_one_issen)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	CHECK(d.OnImpact(10.05, kBandit, ImpactSource::Vanilla).issen);
	const auto second = d.OnImpact(10.08, kDraugr, ImpactSource::Vanilla);
	CHECK(!second.duplicate);
	CHECK(!second.issen);
}

TEST(second_source_of_the_same_swing_is_a_duplicate)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	CHECK(d.OnImpact(10.05, kBandit, ImpactSource::Precision).issen);
	const auto v = d.OnImpact(10.07, kBandit, ImpactSource::Vanilla);
	CHECK(v.duplicate);
	CHECK(!v.issen);
	CHECK_NEAR(v.sinceFirst, 0.02, 1e-4);
}

TEST(next_swing_of_the_same_attacker_is_new)
{
	auto d = Make();
	d.OnImpact(10.0, kBandit, ImpactSource::Vanilla);
	d.OnPress(10.9, Hand::Right);
	const auto v = d.OnImpact(11.0, kBandit, ImpactSource::Vanilla);
	CHECK(!v.duplicate);
	CHECK(v.issen);
}

TEST(press_after_the_hit_is_late)
{
	auto d = Make();
	CHECK(!d.OnImpact(10.0, kBandit, ImpactSource::Vanilla).issen);
	const auto p = d.OnPress(10.1, Hand::Right);
	CHECK(p.kind == PressVerdict::Kind::Late);
	CHECK_NEAR(p.delta, 0.1, 1e-4);
	CHECK(p.attacker == kBandit);
	// only the first late press is reported for a hit
	CHECK(d.OnPress(10.15, Hand::Right).kind == PressVerdict::Kind::None);
}

TEST(press_after_an_issen_hit_is_not_late)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	CHECK(d.OnImpact(10.05, kBandit, ImpactSource::Vanilla).issen);
	CHECK(d.OnPress(10.1, Hand::Right).kind == PressVerdict::Kind::None);
}

TEST(press_long_after_the_hit_is_nothing)
{
	auto d = Make();
	d.OnImpact(10.0, kBandit, ImpactSource::Vanilla);
	CHECK(d.OnPress(10.5, Hand::Right).kind == PressVerdict::Kind::None);
}

TEST(perfect_dodge_then_press_is_issen)
{
	auto d = Make();
	d.OnPerfectDodge(10.0, kDraugr);
	const auto p = d.OnPress(10.3, Hand::Right);
	CHECK(p.kind == PressVerdict::Kind::PerfectDodgeIssen);
	CHECK(p.attacker == kDraugr);
	CHECK_NEAR(p.delta, 0.3, 1e-4);
	// the window is used up
	CHECK(d.OnPress(10.35, Hand::Right).kind == PressVerdict::Kind::None);
}

TEST(perfect_dodge_window_closes)
{
	auto d = Make();
	d.OnPerfectDodge(10.0, kDraugr);
	CHECK(d.OnPress(10.41, Hand::Right).kind == PressVerdict::Kind::None);
}

TEST(perfect_dodge_press_is_not_reused_for_a_hit)
{
	auto d = Make();
	d.OnPerfectDodge(10.0, kDraugr);
	CHECK(d.OnPress(10.1, Hand::Right).kind == PressVerdict::Kind::PerfectDodgeIssen);
	const auto v = d.OnImpact(10.15, kBandit, ImpactSource::Vanilla);
	CHECK(!v.issen);
	CHECK(!v.lead.has_value());
}

TEST(newest_perfect_dodge_wins)
{
	auto d = Make();
	d.OnPerfectDodge(10.0, kBandit);
	d.OnPerfectDodge(10.2, kDraugr);
	const auto p = d.OnPress(10.3, Hand::Right);
	CHECK(p.attacker == kDraugr);
	// the older window is still open
	CHECK(d.OnPress(10.35, Hand::Right).attacker == kBandit);
}

TEST(expire_drops_old_state)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	d.OnPerfectDodge(10.0, kDraugr);
	d.Expire(11.0);
	CHECK(!d.OnImpact(11.0, kBandit, ImpactSource::Vanilla).lead.has_value());
	CHECK(d.OnPress(11.05, Hand::Right).kind == PressVerdict::Kind::Late);  // the hit at 11.0, not the dodge
}

TEST(reset_clears_everything)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	d.OnPerfectDodge(10.0, kDraugr);
	d.Reset();
	CHECK(!d.OnImpact(10.05, kBandit, ImpactSource::Vanilla).issen);
}

TEST(ineligible_hit_keeps_the_press)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	const auto v = d.OnImpact(10.05, kBandit, ImpactSource::Vanilla, false);
	CHECK(!v.issen);
	CHECK(v.lead.has_value());
	CHECK(d.OnImpact(10.08, kDraugr, ImpactSource::Vanilla).issen);
}

TEST(duplicate_of_an_issen_swing_says_so)
{
	auto d = Make();
	d.OnPress(10.0, Hand::Right);
	CHECK(d.OnImpact(10.05, kBandit, ImpactSource::Precision).issen);
	CHECK(d.OnImpact(10.06, kBandit, ImpactSource::Vanilla).firstIssen);
	CHECK(!d.OnImpact(10.07, kDraugr, ImpactSource::Vanilla).duplicate);
	CHECK(!d.OnImpact(10.08, kDraugr, ImpactSource::Vanilla).firstIssen);
}

namespace
{
	Detector MakeGuarded()
	{
		Detector d;
		d.Configure({ .window = 0.12f, .earlyLog = 0.5f, .lateLog = 0.3f, .sameSwing = 0.25f, .perfectWindow = 0.4f, .mashGuard = 0.3f });
		return d;
	}
}

TEST(mashing_makes_no_issen)
{
	auto d = MakeGuarded();
	d.OnPress(9.8, Hand::Right);
	d.OnPress(10.0, Hand::Right);  // 0.2 s after the previous press
	const auto v = d.OnImpact(10.05, kBandit, ImpactSource::Vanilla);
	CHECK(!v.issen);
	CHECK(v.mashed);
}

TEST(a_clean_press_after_a_pause_is_issen)
{
	auto d = MakeGuarded();
	d.OnPress(9.5, Hand::Right);
	d.OnPress(10.0, Hand::Right);  // 0.5 s after the previous press
	const auto v = d.OnImpact(10.05, kBandit, ImpactSource::Vanilla);
	CHECK(v.issen);
	CHECK(!v.mashed);
}

TEST(mashing_through_a_perfect_dodge_window_keeps_it_open)
{
	auto d = MakeGuarded();
	d.OnPress(9.95, Hand::Right);
	d.OnPerfectDodge(10.0, kDraugr);
	const auto mashed = d.OnPress(10.05, Hand::Right);  // 0.1 s after the previous press
	CHECK(mashed.kind == PressVerdict::Kind::None);
	CHECK(mashed.mashed);
	const auto clean = d.OnPress(10.38, Hand::Right);  // 0.33 s later: the window is still there
	CHECK(clean.kind == PressVerdict::Kind::PerfectDodgeIssen);
}
