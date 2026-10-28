# Issen Framework

SKSE-плагин для **Skyrim Special Edition 1.5.97**: исэн в духе Onimusha. Нажми атаку в последний миг перед ударом врага,
или сразу после идеального уклонения, и ответишь одним молниеносным разрезом. Время замедляется, враг замирает,
и через мгновение падает, лишившись головы или руки.

*English summary at the end.*

---

## Возможности

- **Исэн.** Нажатие атаки не раньше чем за `fWindow` (0.12 с) до удара врага. Удар врага отменяется, ты бьёшь в ответ.
- **Исэн после идеального уклонения** (Reactive Dodge Framework): атака в течение `fPerfectDodgeWindow` (0.4 с) после
  идеального уклонения.
- **Защита от спама**: нажатие раньше чем через 0.3 с после предыдущего исэна не даёт.
- **Цена**: исэн тратит выносливость (по умолчанию 20, после уклонения 10). Если её не хватает, исэна нет.
- **Подход к врагу**: перед обычным исэном короткий отскок назад, затем рывок к врагу с призрачным следом. После
  уклонения рывок идёт сразу.
- **Удар**: силовая атака в сторону или на месте, выбирается случайно и ускоряется. Удар засчитывается в кадр попадания
  анимации. На время удара персонаж стоит на месте.
- **Отложенная смерть**, как в Onimusha: враг ещё стоит, его удары не проходят, затем он падает.
- **Расчленение** (Dismembering Framework): по умолчанию голова после уклонения, рука или нога при обычном исэне.
- **Подача**: замедление мира, ч/б картинка, вспышка, кадр-стоп, наезд камеры, горизонтальный слэш, звуки.
- **Правила по врагам** в JSON: кого убивать, кому наносить урон и с каким порогом добивания, что отрубать.
- **Меню FLICK** (F7 → «Исэн»): все настройки меняются на лету, сохраняются в ini.
- Оружие убирается в ножны, когда бой после исэна закончился.

---

## Требования

### Обязательно
| | |
|---|---|
| Skyrim SE **1.5.97** | плагин собран только под эту версию |
| [SKSE64](https://skse.silverlock.org/) | для 1.5.97 |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | версия для SE |

### Необязательные (без каждого пропадает только своя часть)
| Мод | Что даёт | Без него |
|---|---|---|
| Reactive Dodge Framework | исэн после идеального уклонения, отскок перед исэном | только обычный исэн, рывок без отскока |
| [Dismembering Framework](https://www.nexusmods.com/skyrimspecialedition/mods/126203) **и его пакеты ассетов** (гуманоиды, существа) | расчленение | враги просто умирают |
| [Precision](https://www.nexusmods.com/skyrimspecialedition/mods/72347) | точный момент удара врага (по столкновению оружия) | момент удара берётся из игрового попадания |
| GhostTrails | призраки вдоль рывка | рывок без призраков |
| [separated Slash Effects X](https://www.nexusmods.com/skyrimspecialedition/mods/99506) | эффекты слэша и звук разреза | ванильный звук, без эффектов слэша |
| [FLICK](https://www.nexusmods.com/skyrimspecialedition/mods/181603) | меню настроек | настройки только в ini |

Патч Pandora/Nemesis плагину **не нужен**: он использует ванильные события анимаций. Mu Impact Framework для работы
исэна тоже не нужен (см. ниже).

---

## Установка (Mod Organizer 2)

1. Установи обязательные моды и те необязательные, что хочешь.
2. Установи архив **IssenFramework** как обычный мод и включи его. ESP у него нет.
3. **Dismembering Framework**: обязательно поставь оба пакета ассетов, для гуманоидов и для существ. Без них DF ничего
   не отрубает. Проверка: в `Documents\My Games\Skyrim Special Edition\SKSE\DismemberingFramework.log` должны быть
   строки `Parsing JSON Limbs` и `Parsing JSON Data`.
4. **separated Slash Effects X**: если не хочешь, чтобы его слэш играл на каждой обычной атаке, скрой его конфиги MIF
   (`skse\plugins\MuImpactFramework\ssex_*.ini`; в MO2 правый клик по файлу, «Скрыть»). Исэн проигрывает эти
   эффекты сам.
5. **FLICK**: чтобы русский текст в меню не превращался в «???», выбери в редакторе темы FLICK шрифт с кириллицей
   (например, `Jost-Regular`).
6. Запусти игру через SKSE. Проверка: в `Documents\My Games\Skyrim Special Edition\SKSE\IssenFramework.log` должны быть
   строки `rules: ... rule(s)`, `hit hook installed` (после загрузки сохранения) и `menu: 6 pages registered`, если
   стоит FLICK.

Удаление: выключи мод. Плагин ничего не пишет в сохранения. Временное ускорение атаки снимается перед каждым
сохранением.

---

## Как пользоваться

- **Обычный исэн**: дождись замаха врага и нажми атаку в самый последний момент, за доли секунды до попадания.
  Нажал слишком рано — получишь обычную атаку, нажал поздно — получишь удар. Частые нажатия не помогут.
- **Исэн после уклонения** (нужен RDF): увернись в последний момент (идеальное уклонение), сразу атакуй.
- Если в момент исэна враг слишком далеко или сбоку, исэна не будет (см. `[Conditions]`).
- Исэн не работает против драконов и актёров с ключевым словом `Issen_Immune`. Боссы и крупные существа получают урон
  и добиваются только ниже порога здоровья.
- Бессмертные (essential) персонажи уходят в нокдаун без расчленения.
- Закрытый шлем спасает голову, если в настройках DF включено `bPreventFullHelmetBeheading`. Тогда исэн отрубит
  руку или ногу.

---

## Настройка

### Меню FLICK
F7 → группа **«Исэн»**:
- Окно и условия;
- Удар и смерть;
- Время и камера;
- Эффекты;
- Рывок и призраки;
- Отладка.

Изменения применяются сразу. «Сохранить в ini» записывает их в файл, «Перечитать ini» отменяет несохранённое,
«По умолчанию» сбрасывает страницу.

### `SKSE\Plugins\IssenFramework.ini`
Каждый параметр подписан в самом файле. Секции:

| Секция | Что настраивает |
|---|---|
| `[Window]` | окна исэна, защита от спама |
| `[Damage]` | общий урон для всех врагов: % от макс. здоровья, урон оружия, пороги ваншота и добивания, боссы |
| `[Cost]` | расход выносливости: обычный исэн, после уклонения, доля от максимума, нужна ли выносливость |
| `[Perks]` | перки, без которых исэна нет |
| `[Input]` | левая рука, кулаки, дополнительные клавиши атаки |
| `[Conditions]` | дальность и угол до врага |
| `[Execute]` | вкл/выкл, разворот к врагу, расчленение, рывок после уклонения |
| `[Animation]` | варианты удара, ускорение, удар в кадр попадания, фиксация на месте |
| `[Approach]` | отскок, длительность рывка, пауза перед ударом, эффекты появления |
| `[Ghost]` | призраки: количество, интервал, цвет |
| `[Death]` | отложенная смерть, ножны после боя |
| `[Camera]` | кадр-стоп, наезд камеры |
| `[SlowMotion]` | скорость мира и игрока, длительности, цвет |
| `[Effects]` | вспышка, звуки, эффекты слэша (`путь масштаб крен наклон`) |
| `[Compat]` | Precision, событие идеального уклонения RDF |
| `[Dev]` | подробный лог |

Формы указываются как `Plugin.esp|0xFormID` (FormID внутри плагина), например `Skyrim.esm|0x07E8FD`.

### Урон: меню или правила
По умолчанию урон и пороги задаются **правилами** (ниже). Для простой настройки включи в меню **«Урон» → «Урон из
этого меню»** (или `[Damage] bGlobal=1`). Тогда для всех врагов действует одна модель:
- **урон** = доля от максимального здоровья врага + урон оружия × множитель + фиксированная добавка;
- **ваншот**: враг умирает сразу, если у него не больше заданной доли здоровья (1 = всегда, 0 = никогда);
- **добивание**: если после урона у врага осталось не больше заданной доли, он умирает;
- для **боссов** (враг на маркере Boss в локации): свой множитель урона и свои пороги.

Иммунитет (драконы, `Issen_Immune`), расчленение и стаггер при этом всё равно берутся из правил.

### Правила по врагам: `SKSE\Plugins\IssenFramework\Rules\*.json`
Читаются все файлы папки. Из подошедших правил срабатывает правило с наибольшим `priority`. Свои правила лучше класть
отдельным файлом с приоритетом выше стандартных.

```json
{
  "rules": [
    {
      "name": "Бандиты после уклонения: только голова",
      "priority": 500,
      "when": { "keywordsAny": ["ActorTypeNPC"], "kind": "PerfectDodge", "levelMax": 30 },
      "outcome": {
        "mode": "Kill",
        "dismember": { "chance": 1.0, "nodes": [ { "node": "NPC Neck [Neck]", "weight": 1 } ] }
      }
    }
  ]
}
```

- **`when`** (условия):
  - `keywordsAny` / `keywordsAll` / `keywordsNone` — EditorID ключевых слов актёра или его расы;
  - `racesAny` — EditorID рас;
  - `boss` — тип локации Boss;
  - `unique` — уникальный актёр;
  - `levelMin` / `levelMax`;
  - `kind` — `"Normal"` или `"PerfectDodge"`;
  - `playerPerksAny` / `playerPerksAll` / `playerPerksNone` — перки **игрока**: `"Plugin.esp|0xFormID"` или EditorID
    (EditorID перков игра хранит только с модами вроде po3's Tweaks).
- **`outcome.mode`** (результат):
  - `"Kill"` — убить. С `oneShotMaxHealth` (здоровье) или `oneShotMaxHealthPct` (доля от максимума) — убить только
    врага, у которого здоровья не больше порога; у кого больше, тот получает урон по полям ниже (с порогами добивания);
  - `"Damage"` — урон `damageFlat + damageWeaponMult × урон оружия + damageMaxHealthPct × макс. здоровье`; враг
    умирает, если после урона у него остаётся не больше `executeBelowHealthPct` от максимума или
    `executeBelowHealth` здоровья;
  - `"None"` — исэна нет.
- **`outcome.dismember`**: шанс и узлы с весами. Из узлов выбираются только те, что есть у скелета врага. Если DF не
  может отрезать выбранный, плагин пробует следующий. Узлы пакетов DF:
  - люди, драугры, тролли: `NPC Neck [Neck]` (голова), `NPC L/R Forearm [LLar/RLar]`, `NPC L/R Calf [LClf/RClf]`;
  - волки: `Canine_Neck2`, `Canine_LFrontLeg2`, `Canine_RFrontLeg2`, `Canine_LBackLeg2`, `Canine_RBackLeg2`.

Ошибка в JSON (например, опечатка в ключе) пишется в лог, и этот файл пропускается.

#### Перки
- **Допуск к исэну**: `[Perks] sRequirePerk` (обычный исэн) и `sRequirePerkPerfect` (после уклонения), также в
  меню на странице «Окно и условия». Без перка исэна нет, в логе будет `not fired: the player lacks the perk`.
- **Всё остальное — в правилах.** Пример: ваншот людей до 300 здоровья без перка, до 1000 с перком, голова только с
  другим перком:

```json
{
  "rules": [
    {
      "name": "Люди, мастер: ваншот до 1000, голова",
      "priority": 210,
      "when": { "keywordsAny": ["ActorTypeNPC"], "playerPerksAll": ["Skyrim.esm|0x0BABE4", "Skyrim.esm|0x052D50"] },
      "outcome": { "mode": "Kill", "oneShotMaxHealth": 1000, "damageMaxHealthPct": 0.3, "executeBelowHealthPct": 0.25,
                   "dismember": { "nodes": [ { "node": "NPC Neck [Neck]" } ] } }
    },
    {
      "name": "Люди, ученик: ваншот до 1000",
      "priority": 200,
      "when": { "keywordsAny": ["ActorTypeNPC"], "playerPerksAny": ["Skyrim.esm|0x0BABE4"] },
      "outcome": { "mode": "Kill", "oneShotMaxHealth": 1000, "damageMaxHealthPct": 0.3, "executeBelowHealthPct": 0.25,
                   "dismember": { "nodes": [ { "node": "NPC R Forearm [RLar]" }, { "node": "NPC L Forearm [LLar]" } ] } }
    },
    {
      "name": "Люди без перка: ваншот до 300, остальным урон",
      "priority": 190,
      "when": { "keywordsAny": ["ActorTypeNPC"] },
      "outcome": { "mode": "Kill", "oneShotMaxHealth": 300, "damageWeaponMult": 2.0, "executeBelowHealth": 100 }
    }
  ]
}
```

FormID в примере — ванильные перки одноручного оружия: `Skyrim.esm|0x0BABE4` (`Armsman00`, первый ранг) и `0x052D50`
(`FightingStance`); подставь свои. Правила с перками должны стоять выше по приоритету, чем такие же правила без перков.

---

## Совместимость

- Хук попадания ставится последним, после всех плагинов. Исэн видит удар врага раньше RDF и DF, а отменённый удар
  до них не доходит.
- Силовые атаки MCO/BFCO: удар исэна запускается ванильными событиями. Если твой набор анимаций реагирует на них
  иначе, поменяй варианты в `[Animation] sEvent`.
- Замедление: на время удара исэн перехватывает замедление времени (в том числе идеального уклонения RDF), потом
  отдаёт его.
- Плагин меняет поле зрения камеры от третьего лица только на время исэна. Моды камеры, которые сами
  управляют углом обзора, могут с этим спорить: тогда поставь `[Camera] fZoomFov=0`.

## Если что-то не так

Лог: `Documents\My Games\Skyrim Special Edition\SKSE\IssenFramework.log`. С `[Dev] bDevLog=1` (по умолчанию) в нём
есть каждое нажатие, каждый удар врага и решение по нему:
- `ISSEN` — исэн сработал;
- `early by` / `late by` — нажатие раньше или позже окна;
- `not fired: ...` — исэн не сработал, с причиной;
- `struck, dies in`, `died`, `dismembered` — ход отложенной смерти.

---

## Сборка из исходников

- **Нужно**: Visual Studio 2022 (MSVC, C++23), [xmake](https://xmake.io) 3.0+,
  [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng) рядом, в `../CommonLibSSE-NG`.
- **Собрать**:
  ```
  xmake build -y                                     # DLL и тесты
  build\windows\x64\releasedbg\issen_tests.exe       # тесты детектора и правил
  ```
- После сборки DLL, ini (только если его ещё нет) и правила копируются в мод MO2: путь `deploy_dir` в `xmake.lua`.
- **Устройство исходников**:
  - `src/core` — логика без движка, с тестами: окно исэна и правила по врагам;
  - `src/game` — хуки, исполнение, меню.

---

## Благодарности

- **Reactive Dodge Framework**: уклонение, идеальное уклонение и его API.
- **Dismembering Framework** — Seb263: расчленение. Заголовок его API включён в исходники без изменений.
- **separated Slash Effects X** — ap05: эффекты и звуки слэша.
- **GhostTrails**: призрачный след. Заголовок его API включён.
- **Precision** — ersh1: точные попадания. Заголовок его API включён.
- **FLICK** — Fuzzlesz: меню. Заголовок его API и заголовки ImGui (MIT) включены.
- **CommonLibSSE-NG**, **glaze** (MIT).
- Идея — серия **Onimusha** (Capcom).

## Лицензия

**GNU General Public License v3.0**: полный текст в [LICENSE](LICENSE).

Ты можешь использовать, изменять и распространять плагин, в том числе изменённый, при условии, что распространяемая
версия тоже выходит под GPL-3.0 вместе с исходным кодом. Гарантий нет. Сторонние заголовки в `src/third_party` и
`src/game/*API.h` остаются под своими лицензиями (GPL-3.0 или MIT, см. их заголовки).

---

## English summary

**Issen Framework** is an SKSE plugin for Skyrim SE 1.5.97 that adds Onimusha-style *issen*: press attack in the last
instant before an enemy's hit lands (or right after a Reactive Dodge Framework perfect dodge) to cancel that hit and cut
the enemy down. Slow motion, black-and-white frame, hitstop, camera zoom, a ghost-trail dash to the enemy, a sped-up
power attack, Onimusha's delayed death and Dismembering Framework cuts (head after a perfect dodge, an arm or a leg
otherwise).

- **Required**: SKSE64 and Address Library for 1.5.97.
- **Optional**: Reactive Dodge Framework, Dismembering Framework with its humanoid and creature asset packs,
  Precision, GhostTrails, separated Slash Effects X, FLICK. Each one only adds its own part.
- **Settings**: in the FLICK menu (F7 → "Исэн"), in `SKSE\Plugins\IssenFramework.ini`, and per enemy in
  `SKSE\Plugins\IssenFramework\Rules\*.json`.
- **Log**: `Documents\My Games\Skyrim Special Edition\SKSE\IssenFramework.log`.
- **License**: GPL-3.0 (see [LICENSE](LICENSE)).
