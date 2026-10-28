#include "Menu.h"

#include "Executor.h"
#include "Issen.h"
#include "Settings.h"

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

// FLICK's API: ImGui types from the revision FLICK is built with (src/third_party); every call goes through FLICK.
#include "FUCK_API.h"
#include "IconsFontAwesome6.h"

#include <atomic>
#include <format>
#include <string>
#include <vector>

namespace issen::game::menu
{
	namespace
	{
		constexpr auto kPluginName = "IssenFramework";
		constexpr auto kIniPath = "Data/SKSE/Plugins/IssenFramework.ini";

		enum class Page : std::uint8_t
		{
			Window,
			Damage,
			Strike,
			Time,
			Effects,
			Dash,
			Debug,
		};

		enum class Kind : std::uint8_t
		{
			Float,
			Int,
			Bool,
			Text,
			Color,
		};

		// One setting: where it shows, its ini key and how to reach it in Settings.
		struct Field
		{
			Page        page;
			const char* group;    // sub-heading on the page
			const char* section;  // ini
			const char* key;
			const char* label;
			const char* tip;
			Kind        kind;
			float       min{ 0.0f };
			float       max{ 1.0f };
			const char* format{ "%.2f" };
			float* (*f)(Settings&) = nullptr;
			int* (*i)(Settings&) = nullptr;
			bool* (*b)(Settings&) = nullptr;
			std::string* (*s)(Settings&) = nullptr;
			std::array<float, 4>* (*c)(Settings&) = nullptr;
		};

#define ISSEN_F(member) [](Settings& x) { return &x.member; }

		Field F(Page p, const char* g, const char* sec, const char* key, const char* label, const char* tip, float min, float max,
			float* (*get)(Settings&), const char* fmt = "%.2f")
		{
			Field f{ p, g, sec, key, label, tip, Kind::Float, min, max, fmt };
			f.f = get;
			return f;
		}
		Field I(Page p, const char* g, const char* sec, const char* key, const char* label, const char* tip, int min, int max,
			int* (*get)(Settings&))
		{
			Field f{ p, g, sec, key, label, tip, Kind::Int, static_cast<float>(min), static_cast<float>(max) };
			f.i = get;
			return f;
		}
		Field B(Page p, const char* g, const char* sec, const char* key, const char* label, const char* tip, bool* (*get)(Settings&))
		{
			Field f{ p, g, sec, key, label, tip, Kind::Bool };
			f.b = get;
			return f;
		}
		Field T(Page p, const char* g, const char* sec, const char* key, const char* label, const char* tip, std::string* (*get)(Settings&))
		{
			Field f{ p, g, sec, key, label, tip, Kind::Text };
			f.s = get;
			return f;
		}
		Field C(Page p, const char* g, const char* sec, const char* key, const char* label, const char* tip,
			std::array<float, 4>* (*get)(Settings&))
		{
			Field f{ p, g, sec, key, label, tip, Kind::Color };
			f.c = get;
			return f;
		}

		const std::vector<Field>& Fields()
		{
			static const std::vector<Field> fields{
				// ---- window and input
				F(Page::Window, "Окно исэна", "Window", "fWindow", "Окно (с)", "Нажатие атаки не раньше чем за столько секунд до удара врага.",
					0.0f, 0.4f, ISSEN_F(detector.window), "%.3f"),
				F(Page::Window, "Окно исэна", "Window", "fPerfectDodgeWindow", "Окно после уклонения (с)",
					"Атака не позже стольких секунд после идеального уклонения.", 0.0f, 1.5f, ISSEN_F(detector.perfectWindow)),
				F(Page::Window, "Окно исэна", "Window", "fMashGuard", "Защита от спама (с)",
					"Нажатие ближе чем через столько секунд после предыдущего не даёт исэна. 0 = выкл.", 0.0f, 1.0f,
					ISSEN_F(detector.mashGuard)),
				F(Page::Window, "Окно исэна", "Window", "fSameSwing", "Один взмах (с)",
					"Два сообщения об ударе одного врага так близко — это один взмах (Precision и игра).", 0.0f, 1.0f,
					ISSEN_F(detector.sameSwing)),
				B(Page::Window, "Окно исэна", "Cue", "bEnabled", "Свечение для проведения исэна",
					"Вспышка/блик на оружии или теле врага перед ударом и после идеального уклонения (подробности на вкладке «Эффекты»).",
					ISSEN_F(cueEnabled)),
				F(Page::Window, "Условия", "Conditions", "fMaxDistance", "Дальность", "Враг не дальше стольких единиц.", 0.0f, 1000.0f,
					ISSEN_F(maxDistance), "%.0f"),
				F(Page::Window, "Условия", "Conditions", "fMaxAngle", "Угол (°)", "Враг не дальше стольких градусов от взгляда.", 0.0f, 180.0f,
					ISSEN_F(maxAngle), "%.0f"),
				F(Page::Window, "Условия", "Conditions", "fPerfectMaxDistance", "Дальность после уклонения", "", 0.0f, 1500.0f,
					ISSEN_F(perfectMaxDistance), "%.0f"),
				B(Page::Window, "Условия", "Conditions", "bPerfectIgnoreAngle", "Угол не важен после уклонения", "", ISSEN_F(perfectIgnoreAngle)),
				T(Page::Window, "Перки", "Perks", "sRequirePerk", "Перк для исэна",
					"Без этого перка обычного исэна нет. Plugin.esp|0xFormID или EditorID; пусто = доступен всегда.", ISSEN_F(requirePerk)),
				T(Page::Window, "Перки", "Perks", "sRequirePerkPerfect", "Перк для исэна после уклонения", "То же для исэна после уклонения.",
					ISSEN_F(requirePerkPerfect)),
				F(Page::Window, "Выносливость", "Cost", "fStamina", "Цена исэна", "Выносливость за обычный исэн (только когда он сработал).",
					0.0f, 200.0f, ISSEN_F(staminaCost), "%.0f"),
				F(Page::Window, "Выносливость", "Cost", "fStaminaPerfect", "Цена после уклонения", "", 0.0f, 200.0f, ISSEN_F(staminaCostPerfect),
					"%.0f"),
				F(Page::Window, "Выносливость", "Cost", "fStaminaMaxPct", "И доля от максимума", "Добавляется к цене: 0.1 = 10% максимума.",
					0.0f, 1.0f, ISSEN_F(staminaCostMaxPct)),
				B(Page::Window, "Выносливость", "Cost", "bRequireStamina", "Без выносливости исэна нет",
					"Выкл. = исэн срабатывает всё равно и опустошает шкалу.", ISSEN_F(staminaRequired)),
				B(Page::Window, "Ввод", "Input", "bLeftHand", "Левая рука", "Атака левой рукой тоже считается (два оружия).", ISSEN_F(leftHand)),
				B(Page::Window, "Ввод", "Input", "bUnarmed", "Кулаки", "Атаки без оружия тоже считаются.", ISSEN_F(unarmed)),

				// ---- damage
				B(Page::Damage, "Общие настройки", "Damage", "bGlobal", "Урон из этого меню",
					"Вкл. = эти числа для всех врагов вместо чисел правил. Правила по-прежнему решают, кто неуязвим, что отрубается и стаггер.",
					ISSEN_F(damageGlobal)),
				F(Page::Damage, "Урон", "Damage", "fMaxHealthPct", "Урон, доля от макс. здоровья врага", "0.35 = 35% максимального здоровья.", 0.0f,
					1.0f, ISSEN_F(damage.maxHealthPct)),
				F(Page::Damage, "Урон", "Damage", "fWeaponMult", "Плюс урон оружия ×", "Базовый урон твоего оружия, умноженный на это.", 0.0f, 10.0f,
					ISSEN_F(damage.weaponMult)),
				F(Page::Damage, "Урон", "Damage", "fFlat", "Плюс фиксированный урон", "", 0.0f, 1000.0f, ISSEN_F(damage.flat), "%.0f"),
				F(Page::Damage, "Ваншот и добивание", "Damage", "fOneShotBelowPct", "Ваншот, если здоровья не больше",
					"Доля от максимума: враг умирает сразу. 1 = всегда ваншот, 0 = никогда.", 0.0f, 1.0f, ISSEN_F(damage.oneShotBelowPct)),
				F(Page::Damage, "Ваншот и добивание", "Damage", "fExecuteBelowPct", "Добить, если после урона осталось не больше",
					"Доля от максимума. 0 = только если урон снял всё здоровье.", 0.0f, 1.0f, ISSEN_F(damage.executeBelowPct)),
				F(Page::Damage, "Боссы", "Damage", "fBossDamageMult", "Урон по боссам ×", "Боссы — враги на маркере Boss в локации.", 0.0f, 5.0f,
					ISSEN_F(damage.bossDamageMult)),
				F(Page::Damage, "Боссы", "Damage", "fBossOneShotBelowPct", "Ваншот босса, если здоровья не больше", "0 = боссов не ваншотит.", 0.0f,
					1.0f, ISSEN_F(damage.bossOneShotBelowPct)),
				F(Page::Damage, "Боссы", "Damage", "fBossExecuteBelowPct", "Добить босса, если осталось не больше", "", 0.0f, 1.0f,
					ISSEN_F(damage.bossExecuteBelowPct)),

				// ---- strike, animation, death
				B(Page::Strike, "Удар", "Execute", "bEnabled", "Исэн включён", "Выкл. = только запись в лог.", ISSEN_F(execute)),
				B(Page::Strike, "Удар", "Execute", "bFaceTarget", "Развернуть к врагу", "", ISSEN_F(faceTarget)),
				B(Page::Strike, "Удар", "Execute", "bDismember", "Расчленение (DF)", "", ISSEN_F(dismember)),
				T(Page::Strike, "Анимация", "Animation", "sEvent", "Варианты удара",
					"\"событие наклон крен\" через запятую, один выбирается случайно. Наклон и крен (°) поворачивают эффекты удара.",
					ISSEN_F(animEvent)),
				B(Page::Strike, "Анимация", "Animation", "bStopFirst", "Сначала прервать атаку",
					"Перед ударом шлётся attackStop, чтобы удар принялся посреди атаки.", ISSEN_F(animStopFirst)),
				F(Page::Strike, "Анимация", "Animation", "fSpeed", "Ускорение анимации", "", 0.5f, 5.0f, ISSEN_F(animSpeed)),
				F(Page::Strike, "Анимация", "Animation", "fSpeedAfterDash", "Ускорение после рывка", "Для удара, которому предшествовал рывок.",
					0.5f, 5.0f, ISSEN_F(animSpeedAfterDash)),
				F(Page::Strike, "Анимация", "Animation", "fSpeedTime", "Ускорение держится (с)", "", 0.0f, 2.0f, ISSEN_F(animSpeedTime)),
				B(Page::Strike, "Анимация", "Animation", "bStrikeOnHitFrame", "Удар по кадру попадания", "", ISSEN_F(strikeOnHitFrame)),
				F(Page::Strike, "Анимация", "Animation", "fStrikeMaxDelay", "Не позже чем через (с)", "", 0.0f, 2.0f, ISSEN_F(strikeMaxDelay)),
				F(Page::Strike, "Анимация", "Execute", "fStrikeDelay", "Без анимации: удар через (с)", "", 0.0f, 1.0f, ISSEN_F(strikeDelay)),
				B(Page::Strike, "Анимация", "Animation", "bLockPosition", "Стоять на месте во время удара",
					"Анимация удара не сдвигает персонажа.", ISSEN_F(lockPosition)),
				F(Page::Strike, "Анимация", "Animation", "fLockAfter", "И ещё после удара (с)", "", 0.0f, 2.0f, ISSEN_F(lockAfter)),
				B(Page::Strike, "Смерть", "Death", "bDelayed", "Отложенная смерть", "Враг стоит, ты убираешь меч, потом он падает.",
					ISSEN_F(deathDelayed)),
				F(Page::Strike, "Смерть", "Death", "fDelay", "Падает через (с)", "", 0.0f, 3.0f, ISSEN_F(deathDelay)),
				B(Page::Strike, "Смерть", "Death", "bSheathe", "Убрать меч, когда бой окончен", "После убийства исэном.", ISSEN_F(sheathe)),
				F(Page::Strike, "Смерть", "Death", "fSheatheDelay", "Через столько секунд вне боя", "", 0.0f, 5.0f, ISSEN_F(sheatheDelay)),
				T(Page::Strike, "Смерть", "Death", "sSheatheSound", "Звук ножен", "Plugin.esp|0xFormID, пусто = только звук анимации.",
					ISSEN_F(sheatheSound)),

				// ---- time and camera
				F(Page::Time, "Замедление", "SlowMotion", "fWorld", "Скорость мира", "1 = без замедления.", 0.01f, 1.0f, ISSEN_F(slowWorld)),
				F(Page::Time, "Замедление", "SlowMotion", "fPlayer", "Твоя скорость", "", 0.01f, 1.0f, ISSEN_F(slowPlayer)),
				F(Page::Time, "Замедление", "SlowMotion", "fEaseIn", "Вход (с)", "", 0.0f, 1.0f, ISSEN_F(slowEaseIn)),
				F(Page::Time, "Замедление", "SlowMotion", "fHold", "Держится после удара (с)", "", 0.0f, 2.0f, ISSEN_F(slowHold)),
				F(Page::Time, "Замедление", "SlowMotion", "fEaseOut", "Выход (с)", "", 0.0f, 2.0f, ISSEN_F(slowEaseOut)),
				F(Page::Time, "Замедление", "SlowMotion", "fSaturation", "Цвет во время замедления", "0 = чёрно-белое, 1 = как обычно.", 0.0f,
					1.0f, ISSEN_F(saturation)),
				F(Page::Time, "Камера", "Camera", "fHitstop", "Кадр-стоп (с)", "Всё замирает на столько секунд при ударе. 0 = выкл.", 0.0f, 0.4f,
					ISSEN_F(hitstop), "%.3f"),
				F(Page::Time, "Камера", "Camera", "fHitstopScale", "Скорость в кадр-стопе", "", 0.01f, 1.0f, ISSEN_F(hitstopScale)),
				F(Page::Time, "Камера", "Camera", "fZoomFov", "Наезд камеры (°)", "На столько сужается поле зрения. 0 = выкл.", 0.0f, 30.0f,
					ISSEN_F(zoomFov), "%.1f"),
				F(Page::Time, "Камера", "Camera", "fZoomIn", "Наезд (с)", "", 0.0f, 1.0f, ISSEN_F(zoomIn)),
				F(Page::Time, "Камера", "Camera", "fZoomOut", "Отъезд (с)", "", 0.0f, 2.0f, ISSEN_F(zoomOut)),

				// ---- effects
				B(Page::Effects, "Свечение для исэна", "Cue", "bEnabled", "Свечение для проведения исэна",
					"Вкл. = вспышка/свечение на враге, когда открывается окно для исэна.", ISSEN_F(cueEnabled)),
				B(Page::Effects, "Свечение для исэна", "Cue", "bOnSwing", "При замахе врага",
					"Свечение прямо перед попаданием удара врага (обычное окно исэна).", ISSEN_F(cueOnSwing)),
				B(Page::Effects, "Свечение для исэна", "Cue", "bOnPerfectDodge", "После идеального уклонения",
					"Свечение при открытии окна исэна после идеального уклонения RDF.", ISSEN_F(cueOnPerfectDodge)),
				B(Page::Effects, "Свечение для исэна", "Cue", "bOnWeapon", "На оружии врага",
					"Вспышка на оружии/щите/руке врага; если оружия нет — на узле тела ниже.", ISSEN_F(cueOnWeapon)),
				T(Page::Effects, "Свечение для исэна", "Cue", "sNode", "Запасной узел на теле",
					"Узел на теле врага, если у него нет оружия или «На оружии врага» выключено.", ISSEN_F(cueNode)),
				T(Page::Effects, "Свечение для исэна", "Cue", "sVfx", "Эффекты вспышки (NIF)",
					"\"путь масштаб крен наклон\" через запятую; nif-эффекты блика в точке сигнала.", ISSEN_F(cueVfx)),
				F(Page::Effects, "Свечение для исэна", "Cue", "fVfxLifetime", "Длительность вспышки (с)", "", 0.05f, 3.0f,
					ISSEN_F(cueVfxLifetime)),
				T(Page::Effects, "Свечение для исэна", "Cue", "sShader", "Шейдер свечения врага",
					"Plugin.esp|0xFormID EffectShader на враге; пусто = только NIF-вспышка.", ISSEN_F(cueShader)),
				F(Page::Effects, "Свечение для исэна", "Cue", "fShaderDuration", "Длительность шейдера (с)", "", 0.05f, 3.0f,
					ISSEN_F(cueShaderDuration)),
				T(Page::Effects, "Свечение для исэна", "Cue", "sArt", "ArtObject на враге",
					"Путь к .nif (внутри meshes\\), прикрепляемому к врагу; пусто = выкл.", ISSEN_F(cueArt)),
				F(Page::Effects, "Свечение для исэна", "Cue", "fArtDuration", "Длительность ArtObject (с)", "", 0.05f, 3.0f,
					ISSEN_F(cueArtDuration)),
				T(Page::Effects, "Свечение для исэна", "Cue", "sSound", "Звук сигнала",
					"Plugin.esp|0xFormID звука при появлении окна исэна; пусто = без звука.", ISSEN_F(cueSound)),
				F(Page::Effects, "Вспышка и звук", "Effects", "fFlashStrength", "Сила вспышки", "", 0.0f, 1.0f, ISSEN_F(flashStrength)),
				T(Page::Effects, "Вспышка и звук", "Effects", "sFlashImod", "Вспышка", "Plugin.esp|0xFormID модификатора изображения.",
					ISSEN_F(flashImod)),
				T(Page::Effects, "Вспышка и звук", "Effects", "sSlowSound", "Звук замедления", "", ISSEN_F(slowSound)),
				T(Page::Effects, "Вспышка и звук", "Effects", "sStrikeSound", "Звук удара", "Первый найденный из списка.", ISSEN_F(strikeSound)),
				T(Page::Effects, "Слэш", "Effects", "sStrikeVfx", "Эффекты удара",
					"\"путь масштаб крен наклон\" через запятую; nif-эффекты в точке удара.", ISSEN_F(strikeVfx)),
				T(Page::Effects, "Слэш", "Effects", "sStrikeImpactNode", "Узел", "Куда на враге ставятся эффекты.", ISSEN_F(strikeImpactNode)),
				F(Page::Effects, "Слэш", "Effects", "fStrikeVfxLifetime", "Длительность (с)", "", 0.1f, 5.0f, ISSEN_F(strikeVfxLifetime)),

				// ---- dash and ghosts
				B(Page::Dash, "Подход", "Approach", "bPreStep", "Отскок перед обычным исэном", "Уклонение RDF назад, потом рывок к врагу.",
					ISSEN_F(preStep)),
				F(Page::Dash, "Подход", "Approach", "fPreStepAngle", "Угол отскока (°)", "180 = прямо назад.", -180.0f, 180.0f,
					ISSEN_F(preStepAngle), "%.0f"),
				F(Page::Dash, "Подход", "Approach", "fPreStepTime", "Отскок длится (с)", "", 0.0f, 1.0f, ISSEN_F(preStepTime)),
				B(Page::Dash, "Подход", "Execute", "bDashOnPerfectDodge", "Рывок после уклонения", "", ISSEN_F(dashOnPerfect)),
				F(Page::Dash, "Подход", "Approach", "fVanishTime", "Рывок длится (с)", "", 0.0f, 1.0f, ISSEN_F(vanishTime), "%.3f"),
				F(Page::Dash, "Подход", "Approach", "fAppearPause", "Пауза перед ударом (с)", "После рывка, перед ударом.", 0.0f, 1.0f,
					ISSEN_F(appearPause), "%.2f"),
				F(Page::Dash, "Подход", "Execute", "fDashStopDistance", "Остановка перед врагом", "", 0.0f, 300.0f, ISSEN_F(dashStopDistance),
					"%.0f"),
				B(Page::Dash, "Призраки", "Ghost", "bEnabled", "Призраки (GhostTrails)", "", ISSEN_F(ghost)),
				I(Page::Dash, "Призраки", "Ghost", "iCount", "Количество", "", 1, 10, ISSEN_F(ghostCount)),
				F(Page::Dash, "Призраки", "Ghost", "fInterval", "Интервал (с)", "", 0.005f, 0.2f, ISSEN_F(ghostInterval), "%.3f"),
				F(Page::Dash, "Призраки", "Ghost", "fLifetime", "Гаснут за (с)", "", 0.05f, 2.0f, ISSEN_F(ghostLifetime)),
				C(Page::Dash, "Призраки", "Ghost", "sColor", "Цвет", "", ISSEN_F(ghostColor)),
				F(Page::Dash, "Призраки", "Ghost", "fIntensity", "Яркость", "", 0.0f, 10.0f, ISSEN_F(ghostIntensity)),
				F(Page::Dash, "Призраки", "Ghost", "fRimPower", "Контур", "", 0.0f, 10.0f, ISSEN_F(ghostRimPower)),

				// ---- debug
				B(Page::Debug, "Лог", "Dev", "bDevLog", "Подробный лог", "Каждое нажатие, удар и решение в IssenFramework.log.",
					ISSEN_F(devLog)),
				B(Page::Debug, "Совместимость", "Compat", "bUsePrecision", "Удары Precision",
					"Момент удара берётся из Precision (само столкновение). Нужен перезапуск игры.", ISSEN_F(usePrecision)),
				B(Page::Debug, "Совместимость", "Compat", "bPerfectDodge", "Исэн после уклонения RDF",
					"Слушать событие идеального уклонения Reactive Dodge Framework. Нужен перезапуск игры.", ISSEN_F(perfectDodge)),
				B(Page::Debug, "Совместимость", "Compat", "bRealHit", "Настоящий удар оружием",
					"Удар исэна проходит как обычный удар игры (урон задаёт исэн, блок не действует): перки, зачарование, яд, OnHit, реакция врага.",
					ISSEN_F(realHit)),
				B(Page::Debug, "Совместимость", "Compat", "bSendHitEvent", "Событие удара (OnHit)",
					"Без настоящего удара: исэн шлёт событие удара, срабатывают OnHit в Папирусе и механики других модов.", ISSEN_F(sendHitEvent)),
				B(Page::Debug, "Совместимость", "Compat", "bHitPowerAttack", "Как силовая атака",
					"Без настоящего удара: в событии стоит флаг силовой атаки (abPowerAttack).", ISSEN_F(hitPowerAttack)),
				B(Page::Debug, "Совместимость", "Compat", "bHitSpells", "Эффекты при попадании",
					"Без настоящего удара: заклинания перков «при попадании» (кровотечение секир и т.п.) и зачарование оружия.",
					ISSEN_F(hitSpells)),
			};
			return fields;
		}

#undef ISSEN_F

		std::string IniValue(const Field& f, Settings& s)
		{
			switch (f.kind) {
			case Kind::Float: return std::format("{}", *f.f(s));
			case Kind::Int: return std::format("{}", *f.i(s));
			case Kind::Bool: return *f.b(s) ? "1" : "0";
			case Kind::Text: return *f.s(s);
			case Kind::Color:
				{
					const auto& c = *f.c(s);
					return std::format("{}, {}, {}, {}", c[0], c[1], c[2], c[3]);
				}
			}
			return {};
		}

		// The menu edits a copy on the render thread; every change is applied on the main thread (SKSE task).
		class State
		{
		public:
			static State& Get()
			{
				static State s;
				return s;
			}

			Settings& Draft() { return draft_; }

			void Open()
			{
				if (resync_.exchange(false) || !opened_) {
					draft_ = Settings::Get();
					opened_ = true;
				}
			}

			void Resync()
			{
				if (resync_.exchange(false)) {
					draft_ = Settings::Get();
				}
			}

			void Commit()
			{
				unsaved_ = true;
				SKSE::GetTaskInterface()->AddTask([s = draft_]() { Apply(s); });
			}

			void Save()
			{
				SKSE::GetTaskInterface()->AddTask([s = draft_, this]() mutable {
					Apply(s);
					bool ok = true;
					for (const auto& f : Fields()) {
						ok &= ::WritePrivateProfileStringA(f.section, f.key, IniValue(f, s).c_str(), kIniPath) != 0;
					}
					logger::info("menu: settings {} the ini (damage from {}: max health x{}, one shot <= {}, execute <= {})",
						ok ? "saved to" : "could not all be written to", s.damageGlobal ? "the menu" : "the rules", s.damage.maxHealthPct,
						s.damage.oneShotBelowPct, s.damage.executeBelowPct);
					unsaved_ = !ok;
				});
			}

			void Reload()
			{
				SKSE::GetTaskInterface()->AddTask([this]() {
					Settings::Load();
					Configure();
					executor::Init();
					unsaved_ = false;
					resync_ = true;
				});
			}

			bool Unsaved() const { return unsaved_; }

		private:
			static void Apply(const Settings& s)
			{
				Settings::Commit(s);
				Configure();       // the detector's window
				executor::Init();  // forms, strike variants, effects named by the settings
			}

			Settings          draft_;
			bool              opened_{ false };
			std::atomic<bool> unsaved_{ false };
			std::atomic<bool> resync_{ false };
		};

		void DrawField(const Field& f)
		{
			auto& s = State::Get().Draft();
			bool  commit = false;
			switch (f.kind) {
			case Kind::Float:
				commit = FUCK::SliderFloat(f.label, f.f(s), f.min, f.max, f.format);
				break;
			case Kind::Int:
				commit = FUCK::SliderInt(f.label, f.i(s), static_cast<int>(f.min), static_cast<int>(f.max));
				break;
			case Kind::Bool:
				commit = FUCK::Checkbox(f.label, f.b(s));
				break;
			case Kind::Color:
				commit = FUCK::ColorEdit4(f.label, f.c(s)->data(), FUCK::ColorEditFlags::kAlphaPreviewHalf);
				break;
			case Kind::Text:
				FUCK::InputText(f.label, f.s(s));
				commit = FUCK::IsItemDeactivatedAfterEdit();  // half-typed text is never applied
				break;
			}
			if (f.tip && *f.tip && FUCK::IsItemHovered()) {
				FUCK::SetTooltip(f.tip);  // tips carry no '%': the host formats them printf-style
			}
			if (commit) {
				State::Get().Commit();
			}
		}

		class PageTool final : public FUCK::ITool
		{
		public:
			PageTool(Page page, const char* name, const char* icon, const char* note) : page_(page), name_(name), icon_(icon), note_(note) {}

			const char* Name() const override { return name_; }
			const char* Group() const override { return "Исэн"; }
			void        OnOpen() override { State::Get().Open(); }

			void Draw() override
			{
				auto& state = State::Get();
				state.Resync();
				FUCK::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", FUCK::IconText(icon_, name_));
				if (note_) {
					FUCK::TextDisabledWrapped("%s", note_);
				}
				FUCK::Separator();
				FUCK::Spacing();
				const char* group = nullptr;
				for (const auto& f : Fields()) {
					if (f.page != page_) {
						continue;
					}
					if (!group || std::strcmp(group, f.group) != 0) {
						group = f.group;
						FUCK::SeparatorText(group);
					}
					// The damage numbers only count with "damage from this menu" on: greyed out otherwise.
					// Similarly, Cue sub-options are greyed out when the Cue checkbox is off.
					const bool inactiveDamage = page_ == Page::Damage && std::strcmp(f.key, "bGlobal") != 0 && !state.Draft().damageGlobal;
					const bool inactiveCue = std::strcmp(f.section, "Cue") == 0 && std::strcmp(f.key, "bEnabled") != 0 && !state.Draft().cueEnabled;
					const bool inactive = inactiveDamage || inactiveCue;
					FUCK::PushID(f.section);
					FUCK::PushID(f.key);
					if (inactive) {
						FUCK::BeginDisabled();
					}
					DrawField(f);
					if (inactive) {
						FUCK::EndDisabled();
					}
					FUCK::PopID();
					FUCK::PopID();
					if (page_ == Page::Damage && std::strcmp(f.key, "bGlobal") == 0) {
						if (state.Draft().damageGlobal) {
							FUCK::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", "Сейчас урон берётся из этого меню.");
						} else {
							FUCK::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
								"Сейчас урон берётся из правил (Rules). Включи галочку выше, чтобы числа ниже заработали.");
						}
					}
				}
				DrawFooter();
			}

		private:
			void DrawFooter()
			{
				FUCK::Spacing(2);
				FUCK::Separator();
				FUCK::Spacing();
				auto& state = State::Get();
				if (FUCK::Button(FUCK::IconText(ICON_FA_FLOPPY_DISK, "Сохранить в ini"))) {
					state.Save();
				}
				FUCK::SameLine();
				if (FUCK::Button(FUCK::IconText(ICON_FA_ARROWS_ROTATE, "Перечитать ini"))) {
					state.Reload();
				}
				FUCK::SameLine();
				if (FUCK::Button(FUCK::IconText(ICON_FA_ROTATE_LEFT, "По умолчанию"))) {
					const Settings defaults{};
					auto&          draft = state.Draft();
					for (const auto& f : Fields()) {
						if (f.page != page_) {
							continue;
						}
						auto& d = const_cast<Settings&>(defaults);
						switch (f.kind) {
						case Kind::Float: *f.f(draft) = *f.f(d); break;
						case Kind::Int: *f.i(draft) = *f.i(d); break;
						case Kind::Bool: *f.b(draft) = *f.b(d); break;
						case Kind::Text: *f.s(draft) = *f.s(d); break;
						case Kind::Color: *f.c(draft) = *f.c(d); break;
						}
					}
					state.Commit();
				}
				if (state.Unsaved()) {
					FUCK::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", "Изменения применены, но не сохранены в ini");
				} else {
					FUCK::TextDisabled("%s", "Изменения применяются сразу");
				}
			}

			Page        page_;
			const char* name_;
			const char* icon_;
			const char* note_;
		};

		// FLICK keeps the pointers: the tools live as long as the plugin.
		std::vector<PageTool>& Tools()
		{
			static std::vector<PageTool> tools{
				{ Page::Window, "Окно и условия", ICON_FA_STOPWATCH, "Когда нажатие атаки становится исэном." },
				{ Page::Damage, "Урон", ICON_FA_HEART_CRACK,
					"Урон исэна и пороги ваншота и добивания. Без «Урон из этого меню» действуют числа правил (Rules\\*.json)." },
				{ Page::Strike, "Удар и смерть", ICON_FA_KHANDA, "Анимация удара, расчленение и отложенная смерть." },
				{ Page::Time, "Время и камера", ICON_FA_HOURGLASS_HALF, "Замедление, кадр-стоп, наезд камеры, цвет." },
				{ Page::Effects, "Эффекты", ICON_FA_WAND_MAGIC_SPARKLES, "Свечение для исэна, вспышка, звуки и эффекты слэша." },
				{ Page::Dash, "Рывок и призраки", ICON_FA_PERSON_RUNNING, "Отскок, рывок к врагу и призрачный след." },
				{ Page::Debug, "Отладка", ICON_FA_BUG, nullptr },
			};
			return tools;
		}
	}

	void Install()
	{
		if (!::GetModuleHandleW(L"FUCK.dll")) {
			logger::info("menu: FLICK not loaded, in-game settings menu disabled");
			return;
		}
		if (!FUCK::Connect(kPluginName)) {
			logger::warn("menu: FLICK found but the API connection failed (version mismatch?)");
			return;
		}
		for (auto& tool : Tools()) {
			FUCK::RegisterTool(&tool);
		}
		logger::info("menu: {} pages registered with FLICK", Tools().size());
	}
}
