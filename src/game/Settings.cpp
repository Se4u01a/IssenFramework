#include "Settings.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace issen::game
{
	namespace
	{
		constexpr auto kIniPath = "Data/SKSE/Plugins/IssenFramework.ini";

		Settings g_settings;

		std::string ReadString(const char* section, const char* key, const std::string& def)
		{
			char buffer[4096]{};
			::GetPrivateProfileStringA(section, key, def.c_str(), buffer, sizeof(buffer), kIniPath);
			return buffer;
		}

		float ReadFloat(const char* section, const char* key, float def, float lo, float hi)
		{
			const auto text = ReadString(section, key, "");
			if (text.empty()) {
				return def;
			}
			char*       end = nullptr;
			const float value = std::strtof(text.c_str(), &end);
			if (end == text.c_str() || !std::isfinite(value)) {
				logger::warn("[{}] {}: '{}' is not a number, using {}", section, key, text, def);
				return def;
			}
			const float clamped = std::clamp(value, lo, hi);
			if (clamped != value) {
				logger::warn("[{}] {}: {} out of range [{}, {}], using {}", section, key, value, lo, hi, clamped);
			}
			return clamped;
		}

		bool ReadBool(const char* section, const char* key, bool def)
		{
			const auto text = ReadString(section, key, "");
			if (text.empty()) {
				return def;
			}
			return std::strtol(text.c_str(), nullptr, 0) != 0;
		}

		// "a, b, c": decimal or 0x hex codes.
		std::vector<int> ReadCodes(const char* section, const char* key)
		{
			std::vector<int>  out;
			std::stringstream ss(ReadString(section, key, ""));
			std::string       item;
			while (std::getline(ss, item, ',')) {
				char*      end = nullptr;
				const long value = std::strtol(item.c_str(), &end, 0);
				if (end != item.c_str() && value >= 0) {
					out.push_back(static_cast<int>(value));
				}
			}
			return out;
		}
	}

	const Settings& Settings::Get()
	{
		return g_settings;
	}

	void Settings::Commit(const Settings& s)
	{
		g_settings = s;
	}

	void Settings::Load()
	{
		Settings s;
		auto&    d = s.detector;
		d.window = ReadFloat("Window", "fWindow", d.window, 0.0f, 1.0f);
		d.perfectWindow = ReadFloat("Window", "fPerfectDodgeWindow", d.perfectWindow, 0.0f, 3.0f);
		d.earlyLog = ReadFloat("Window", "fEarlyLog", d.earlyLog, 0.0f, 3.0f);
		d.lateLog = ReadFloat("Window", "fLateLog", d.lateLog, 0.0f, 3.0f);
		d.sameSwing = ReadFloat("Window", "fSameSwing", d.sameSwing, 0.0f, 1.0f);
		d.mashGuard = ReadFloat("Window", "fMashGuard", d.mashGuard, 0.0f, 2.0f);

		s.leftHand = ReadBool("Input", "bLeftHand", s.leftHand);
		s.unarmed = ReadBool("Input", "bUnarmed", s.unarmed);
		s.extraKeyboard = ReadCodes("Input", "sExtraKeyboard");
		s.extraMouse = ReadCodes("Input", "sExtraMouse");
		s.extraGamepad = ReadCodes("Input", "sExtraGamepad");

		s.maxDistance = ReadFloat("Conditions", "fMaxDistance", s.maxDistance, 0.0f, 5000.0f);
		s.maxAngle = ReadFloat("Conditions", "fMaxAngle", s.maxAngle, 0.0f, 180.0f);
		s.perfectMaxDistance = ReadFloat("Conditions", "fPerfectMaxDistance", s.perfectMaxDistance, 0.0f, 5000.0f);
		s.perfectIgnoreAngle = ReadBool("Conditions", "bPerfectIgnoreAngle", s.perfectIgnoreAngle);

		s.requirePerk = ReadString("Perks", "sRequirePerk", s.requirePerk);
		s.requirePerkPerfect = ReadString("Perks", "sRequirePerkPerfect", s.requirePerkPerfect);

		s.damageGlobal = ReadBool("Damage", "bGlobal", s.damageGlobal);
		auto& g = s.damage;
		g.maxHealthPct = ReadFloat("Damage", "fMaxHealthPct", g.maxHealthPct, 0.0f, 2.0f);
		g.weaponMult = ReadFloat("Damage", "fWeaponMult", g.weaponMult, 0.0f, 20.0f);
		g.flat = ReadFloat("Damage", "fFlat", g.flat, 0.0f, 100000.0f);
		g.oneShotBelowPct = ReadFloat("Damage", "fOneShotBelowPct", g.oneShotBelowPct, 0.0f, 1.0f);
		g.executeBelowPct = ReadFloat("Damage", "fExecuteBelowPct", g.executeBelowPct, 0.0f, 1.0f);
		g.bossDamageMult = ReadFloat("Damage", "fBossDamageMult", g.bossDamageMult, 0.0f, 10.0f);
		g.bossOneShotBelowPct = ReadFloat("Damage", "fBossOneShotBelowPct", g.bossOneShotBelowPct, 0.0f, 1.0f);
		g.bossExecuteBelowPct = ReadFloat("Damage", "fBossExecuteBelowPct", g.bossExecuteBelowPct, 0.0f, 1.0f);

		s.staminaCost = ReadFloat("Cost", "fStamina", s.staminaCost, 0.0f, 1000.0f);
		s.staminaCostPerfect = ReadFloat("Cost", "fStaminaPerfect", s.staminaCostPerfect, 0.0f, 1000.0f);
		s.staminaCostMaxPct = ReadFloat("Cost", "fStaminaMaxPct", s.staminaCostMaxPct, 0.0f, 1.0f);
		s.staminaRequired = ReadBool("Cost", "bRequireStamina", s.staminaRequired);

		s.execute = ReadBool("Execute", "bEnabled", s.execute);
		s.strikeDelay = ReadFloat("Execute", "fStrikeDelay", s.strikeDelay, 0.0f, 2.0f);
		s.faceTarget = ReadBool("Execute", "bFaceTarget", s.faceTarget);
		s.dashOnPerfect = ReadBool("Execute", "bDashOnPerfectDodge", s.dashOnPerfect);
		s.dashStopDistance = ReadFloat("Execute", "fDashStopDistance", s.dashStopDistance, 0.0f, 500.0f);
		s.dismember = ReadBool("Execute", "bDismember", s.dismember);

		s.animEvent = ReadString("Animation", "sEvent", s.animEvent);
		s.animStopFirst = ReadBool("Animation", "bStopFirst", s.animStopFirst);
		s.animSpeed = ReadFloat("Animation", "fSpeed", s.animSpeed, 0.1f, 10.0f);
		s.animSpeedAfterDash = ReadFloat("Animation", "fSpeedAfterDash", s.animSpeedAfterDash, 0.1f, 10.0f);
		s.animSpeedTime = ReadFloat("Animation", "fSpeedTime", s.animSpeedTime, 0.0f, 5.0f);
		s.strikeOnHitFrame = ReadBool("Animation", "bStrikeOnHitFrame", s.strikeOnHitFrame);
		s.strikeMaxDelay = ReadFloat("Animation", "fStrikeMaxDelay", s.strikeMaxDelay, 0.0f, 3.0f);
		s.lockPosition = ReadBool("Animation", "bLockPosition", s.lockPosition);
		s.lockAfter = ReadFloat("Animation", "fLockAfter", s.lockAfter, 0.0f, 3.0f);

		s.preStep = ReadBool("Approach", "bPreStep", s.preStep);
		s.preStepAngle = ReadFloat("Approach", "fPreStepAngle", s.preStepAngle, -180.0f, 180.0f);
		s.preStepTime = ReadFloat("Approach", "fPreStepTime", s.preStepTime, 0.0f, 2.0f);
		s.appearPause = ReadFloat("Approach", "fAppearPause", s.appearPause, 0.0f, 2.0f);
		s.vanishTime = ReadFloat("Approach", "fVanishTime", s.vanishTime, 0.0f, 2.0f);
		s.vanishShader = ReadString("Approach", "sVanishShader", s.vanishShader);
		s.appearShader = ReadString("Approach", "sAppearShader", s.appearShader);
		s.vanishArt = ReadString("Approach", "sVanishArt", s.vanishArt);
		s.appearArt = ReadString("Approach", "sAppearArt", s.appearArt);
		s.approachFxTime = ReadFloat("Approach", "fFxTime", s.approachFxTime, 0.0f, 5.0f);

		s.ghost = ReadBool("Ghost", "bEnabled", s.ghost);
		s.ghostCount = static_cast<int>(ReadFloat("Ghost", "iCount", static_cast<float>(s.ghostCount), 1.0f, 10.0f));
		s.ghostInterval = ReadFloat("Ghost", "fInterval", s.ghostInterval, 0.005f, 1.0f);
		s.ghostLifetime = ReadFloat("Ghost", "fLifetime", s.ghostLifetime, 0.05f, 5.0f);
		{
			const auto text = ReadString("Ghost", "sColor", "");
			std::array<float, 4> c{};
			if (std::sscanf(text.c_str(), "%f , %f , %f , %f", &c[0], &c[1], &c[2], &c[3]) == 4) {
				for (auto& v : c) {
					v = std::clamp(v, 0.0f, 1.0f);
				}
				s.ghostColor = c;
			}
		}
		s.ghostIntensity = ReadFloat("Ghost", "fIntensity", s.ghostIntensity, 0.0f, 20.0f);
		s.ghostRimPower = ReadFloat("Ghost", "fRimPower", s.ghostRimPower, 0.0f, 10.0f);

		s.deathDelayed = ReadBool("Death", "bDelayed", s.deathDelayed);
		s.deathDelay = ReadFloat("Death", "fDelay", s.deathDelay, 0.0f, 5.0f);
		s.sheathe = ReadBool("Death", "bSheathe", s.sheathe);
		s.sheatheDelay = ReadFloat("Death", "fSheatheDelay", s.sheatheDelay, 0.0f, 5.0f);
		s.sheatheSound = ReadString("Death", "sSheatheSound", s.sheatheSound);
		s.killVerifyTime = ReadFloat("Death", "fKillVerifyTime", s.killVerifyTime, 0.05f, 5.0f);

		s.hitstop = ReadFloat("Camera", "fHitstop", s.hitstop, 0.0f, 1.0f);
		s.hitstopScale = ReadFloat("Camera", "fHitstopScale", s.hitstopScale, 0.01f, 1.0f);
		s.zoomFov = ReadFloat("Camera", "fZoomFov", s.zoomFov, 0.0f, 40.0f);
		s.zoomIn = ReadFloat("Camera", "fZoomIn", s.zoomIn, 0.0f, 2.0f);
		s.zoomOut = ReadFloat("Camera", "fZoomOut", s.zoomOut, 0.0f, 3.0f);

		s.saturation = ReadFloat("SlowMotion", "fSaturation", s.saturation, 0.0f, 1.0f);
		s.slowWorld = ReadFloat("SlowMotion", "fWorld", s.slowWorld, 0.01f, 1.0f);
		s.slowPlayer = ReadFloat("SlowMotion", "fPlayer", s.slowPlayer, 0.01f, 1.0f);
		s.slowEaseIn = ReadFloat("SlowMotion", "fEaseIn", s.slowEaseIn, 0.0f, 3.0f);
		s.slowHold = ReadFloat("SlowMotion", "fHold", s.slowHold, 0.0f, 5.0f);
		s.slowEaseOut = ReadFloat("SlowMotion", "fEaseOut", s.slowEaseOut, 0.0f, 3.0f);

		s.flashImod = ReadString("Effects", "sFlashImod", s.flashImod);
		s.flashStrength = ReadFloat("Effects", "fFlashStrength", s.flashStrength, 0.0f, 1.0f);
		s.strikeSound = ReadString("Effects", "sStrikeSound", s.strikeSound);
		s.slowSound = ReadString("Effects", "sSlowSound", s.slowSound);
		s.strikeImpacts = ReadString("Effects", "sStrikeImpacts", s.strikeImpacts);
		s.strikeImpactNode = ReadString("Effects", "sStrikeImpactNode", s.strikeImpactNode);
		s.strikeArt = ReadString("Effects", "sStrikeArt", s.strikeArt);
		s.strikeArtDuration = ReadFloat("Effects", "fStrikeArtDuration", s.strikeArtDuration, 0.0f, 10.0f);
		s.strikeVfx = ReadString("Effects", "sStrikeVfx", s.strikeVfx);
		s.strikeVfxLifetime = ReadFloat("Effects", "fStrikeVfxLifetime", s.strikeVfxLifetime, 0.1f, 10.0f);

		s.cueEnabled = ReadBool("Cue", "bEnabled", s.cueEnabled);
		s.cueOnSwing = ReadBool("Cue", "bOnSwing", s.cueOnSwing);
		s.cueOnPerfectDodge = ReadBool("Cue", "bOnPerfectDodge", s.cueOnPerfectDodge);
		s.cueOnWeapon = ReadBool("Cue", "bOnWeapon", s.cueOnWeapon);
		s.cueNode = ReadString("Cue", "sNode", s.cueNode);
		s.cueVfx = ReadString("Cue", "sVfx", s.cueVfx);
		s.cueVfxLifetime = ReadFloat("Cue", "fVfxLifetime", s.cueVfxLifetime, 0.05f, 10.0f);
		s.cueArt = ReadString("Cue", "sArt", s.cueArt);
		s.cueArtDuration = ReadFloat("Cue", "fArtDuration", s.cueArtDuration, 0.0f, 10.0f);
		s.cueShader = ReadString("Cue", "sShader", s.cueShader);
		s.cueShaderDuration = ReadFloat("Cue", "fShaderDuration", s.cueShaderDuration, 0.0f, 10.0f);
		s.cueSound = ReadString("Cue", "sSound", s.cueSound);

		s.usePrecision = ReadBool("Compat", "bUsePrecision", s.usePrecision);
		s.perfectDodge = ReadBool("Compat", "bPerfectDodge", s.perfectDodge);
		s.realHit = ReadBool("Compat", "bRealHit", s.realHit);
		s.sendHitEvent = ReadBool("Compat", "bSendHitEvent", s.sendHitEvent);
		s.hitPowerAttack = ReadBool("Compat", "bHitPowerAttack", s.hitPowerAttack);
		s.hitSpells = ReadBool("Compat", "bHitSpells", s.hitSpells);
		s.rdfModEvent = ReadString("Compat", "sRdfPerfectDodgeEvent", s.rdfModEvent);

		s.devLog = ReadBool("Dev", "bDevLog", s.devLog);

		g_settings = std::move(s);
		logger::info("settings: execute={} strikeDelay={:.2f}s slow world x{:.2f} player x{:.2f} hold {:.2f}s flash '{}' sounds '{}' '{}' cue={}",
			g_settings.execute, g_settings.strikeDelay, g_settings.slowWorld, g_settings.slowPlayer, g_settings.slowHold,
			g_settings.flashImod, g_settings.strikeSound, g_settings.slowSound, g_settings.cueEnabled);
		logger::info("settings: window={:.3f}s perfectDodgeWindow={:.3f}s early/late log {:.2f}/{:.2f}s sameSwing={:.2f}s "
					 "leftHand={} unarmed={} extra keys kb/mouse/pad {}/{}/{} precision={} perfectDodge={} ('{}') devLog={}",
			d.window, d.perfectWindow, d.earlyLog, d.lateLog, d.sameSwing, g_settings.leftHand, g_settings.unarmed,
			g_settings.extraKeyboard.size(), g_settings.extraMouse.size(), g_settings.extraGamepad.size(),
			g_settings.usePrecision, g_settings.perfectDodge, g_settings.rdfModEvent, g_settings.devLog);
	}
}
