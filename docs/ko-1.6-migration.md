# CrossPoint 1.6 KO migration

## Source of truth

The official upstream `1.6.0` tag points to
`54337e6d73fc628f4ba523ddc89a743ca8c6e4c5`. The SHA
`8c84ef3268fd56147ae1625e04f773f0f50b7720` in the work order is a later
commit whose `platformio.ini` says `1.6.5`; it is not the 1.6.0 base. This branch
starts at the tag commit. The KO source is `84a39194dfce1ebd772ac9163df0a59daa0d72dc`
(`1.5.0-ko.3`), descended from `e00f5958dfeea2a3e640c39eb78186fd20996f4b`.

The work is a feature port. Do not replay the KO commit series or replace entire
upstream 1.6 renderer, reader, font cache, input, or HAL files with their 1.5
counterparts.

## KO feature inventory before code changes

`KEEP` means the user-facing behavior must survive. `REIMPLEMENT` means it
needs a new implementation on the 1.6 structure. `UPSTREAM_REPLACEMENT` means
1.6 already provides the relevant behavior. `REVIEW` requires a build or device
decision. These are port decisions, not claims that work is complete.

| Feature from KO 1.5.0-ko.3 | Decision | 1.6 integration point and reason |
| --- | --- | --- |
| Korean default UI and English fallback | KEEP, REIMPLEMENT | Keep upstream translation sources; select ko/en during generation if size requires it. Validate `language.bin` index migration. |
| KoPub Batang body glyphs | KEEP, REIMPLEMENT | Add the KO font assets and IDs to the 1.6 built-in font manifest. Measure flash cost. |
| Pretendard UI glyphs | KEEP, REIMPLEMENT | Keep firmware glyph metrics and evaluate the KS X 1001 subset against each partition. |
| `UnifiedFontFamily`, `SdFont`, `SdFontFamily` | KEEP, REIMPLEMENT | Adapt the KO font adapters to upstream `GfxRenderer` and `FontCacheManager`, preserving its prewarm and cache release improvements. |
| `.epdfont` format and SD glyph lookup | KEEP, REIMPLEMENT | Retain interval and glyph metadata format for existing user files; keep upstream SD cache pressure handling. |
| `customFontPath`, `systemFontPath`, font selection | KEEP, REIMPLEMENT | Persist old paths and connect to 1.6 font UI. |
| Glyph fallback, synthetic bold, letter spacing | KEEP, REIMPLEMENT | Preserve KO typography while using 1.6 glyph arena and cache behavior. |
| Korean character wrap and spacing bound | KEEP, REIMPLEMENT | Extend 1.6 `ParsedText`; respect `wordContinues` and `wordNoSpaceBefore`. |
| Deferred last line across soft flush | KEEP, REIMPLEMENT | Test 320, 500, 1000, and multi-thousand-token input for no dropped text. Commit consumption only after line commit. |
| Explicit paragraph indent | KEEP, REIMPLEMENT | Apply once at paragraph start, including after a soft flush; avoid doubling implicit indent. |
| KO line spacing 1.00/1.20/1.40 | KEEP, REIMPLEMENT | Map to 1.6 reader settings; retain upstream Extra Wide as an additional option. |
| Hyphenation off with character wrap | KEEP, REIMPLEMENT | Make character-wrap setting control hyphenation without deleting 1.6 fixes. Review `CP_HYPHENATION_LANGS` with size results. |
| KO reader options screen | UPSTREAM_REPLACEMENT | Place settings in 1.6 Reader Toolbar / FUI flow instead of restoring the old activity. |
| Reading statistics and per-book timer | KEEP, REIMPLEMENT | Read existing `reading_stats.bin`; count only active reader time. |
| Auto page turn | KEEP, REIMPLEMENT | Bind to 1.6 reader lifecycle for EPUB and TXT; cancel on exit, sleep, and overlays. |
| Long-press chapter/page behavior | UPSTREAM_REPLACEMENT, REVIEW | 1.6 already has `longPressButtonBehavior` and `MappedInputManager` held-time handling; compare KO edge behavior before extending. |
| Short Back to file browser | UPSTREAM_REPLACEMENT, REVIEW | 1.6 already has `backShortToFileBrowser`; verify footnote and overlay priority. |
| Sleep image selection | KEEP, REIMPLEMENT | Keep KO selection data, compose with 1.6 transparent sleep overlays. |
| X3/X4 raw-SD staged OTA | REVIEW | Retain only if image verification and partition tests justify it. |
| X4 Pro raw-SD OTA | DROP unless proven needed | Use the official 1.6 X4 Pro OTA path first. |
| KO release endpoint and version parser | KEEP, REIMPLEMENT | Update endpoint and parser without replacing 1.6 networking/device profiles. |
| X4 Pro hardware drivers | UPSTREAM_REPLACEMENT | Use upstream 1.6 board, touch, frontlight, Home key, USB and sleep HAL. |
| Legacy `TextSettingsActivity` removal | REVIEW | 1.6 reader flow determines whether any KO-specific screen is needed. |
| Legacy battery calibration | REVIEW | Check 1.6 board behavior on hardware before carrying device-specific code. |

## Three-way comparison priorities

The 1.5 common ancestor, 1.6 tag, and KO tip all modify `ParsedText`,
`GfxRenderer`, `FontCacheManager`, settings, and reader activities. Font assets
and `.epdfont` are mostly KO additions; X4 Pro and the new toolbar are 1.6
additions. Treat changes at the feature level. In particular, 1.6 `ParsedText`
already has deque-backed CJK tokens and four boundary-flag combinations, so
the KO wrap algorithm must operate on those semantics rather than its old token
container assumptions.

The KO 1.5 tree ships only KoPub Batang and Pretendard as built-ins. The first
1.6 build with these fonts alongside Noto Serif, Noto Sans, and Ubuntu exceeded
the default 6,553,600-byte app partition by 409,537 bytes. The active built-in
font profile now contains only KoPub Batang 14pt and Pretendard 10pt, as in the
KO lineage. Their bitmaps were regenerated with 1.6's grouped DEFLATE format:
KoPub 2,868,311 to 999,047 bytes, Pretendard 933,543 to 322,904 bytes.
The 1.6 font cache and SD `.cpfont` infrastructure remain in use. Legacy
`.epdfont` compatibility and user-selected system font paths still need work.

## Baseline and gates

| Baseline | Result |
| --- | --- |
| KO 1.5 host CMake configure under sandbox | Failed because Windows SDK path was denied. With SDK access it configured. Its unmodified MSVC build failed on GCC `-Wextra`. With only a compiler-option workaround in the isolated baseline worktree, 130/130 host tests pass. |
| Upstream 1.6 host CMake configure with SDK access | Passed with MSVC 19.44.35213 and CMake 4.4.0-rc3. |
| Upstream 1.6 host build | Initial baseline failed on GCC flags, UTF-8 source handling, Expat linkage, and an MSVC parser-test macro. Windows host fixes now build successfully. |
| Upstream 1.6 host tests | 178/178 pass with MSVC 19.44.35213, including KO language migration and built-in font coverage tests. |
| Upstream 1.6 `default` firmware | Passed with repository-local PlatformIO 6.1.18 after a full ESP-IDF rebuild. `firmware.bin` image reported 5,488,603 bytes; program flash usage 5,475,051 / 6,553,600 bytes (83.5%). |
| Upstream 1.6 `x4pro` firmware | Passed. `firmware.bin` is 5,379,280 bytes; program flash usage 5,378,766 / 6,553,600 bytes (82.1%). |
| KO 1.5 `default` firmware | Passed in an isolated worktree. `firmware.bin` is 5,932,160 bytes; program flash usage 5,918,133 / 6,946,816 bytes (85.2%). The old image patch/verification scripts both passed. |
| Current 1.6 KO `default` firmware | Passed after decoupling keyboard layout codes from the reduced language enum. `firmware.bin` is 5,141,216 bytes; program flash usage 5,127,503 / 6,553,600 bytes (78.2%). |
| Current 1.6 KO `x4pro` firmware | Passed with the same changes. `firmware.bin` is 5,031,952 bytes; program flash usage 5,031,446 / 6,553,600 bytes (76.8%). |
| KO-only font profile `default` firmware | Passed with KoPub/Pretendard as the only registered built-ins. `firmware.bin` is 4,823,664 bytes; program flash usage 4,809,945 / 6,553,600 bytes (73.4%). |
| KO-only font profile `x4pro` firmware | Passed with the same font profile. `firmware.bin` is 4,715,072 bytes; program flash usage 4,714,558 / 6,553,600 bytes (71.9%). |

Firmware toolchains: riscv32-esp-elf-g++ and xtensa-esp-elf-g++ 14.2.0
(esp-14.2.0_20251107). The KO baseline worktree contains a host-only MSVC CMake
option workaround, so its build banner says `dirty`; the firmware source itself
is the KO tip commit.

PlatformIO 6.2.0 failed here with a SCons `FortranCommon` import error. Both
firmware builds need `PYTHONIOENCODING=utf-8` in this Windows Korean locale;
otherwise generation fails while printing an Arabic translation.

Upstream 1.6 reads a language code in `settings.json` and no longer reads
`language.bin`; the latter remains on KO 1.5 cards. The migration resolves
the old versioned numeric index **only when settings JSON is absent**,
then persist a code string. Version 1 index 11 means Korean in the KO lineage.
Current KO defaults also need an explicit Korean settings value: an `I18n`
constructor default alone is overwritten by `SETTINGS.language` during boot.

The generator now accepts `--languages` while retaining all upstream YAML files.
The firmware's SCons entry point selects English and Korean. The KO YAML carries
the 32 new upstream 1.6 reader toolbar, touch, frontlight, and USB strings.
New cards default to Korean in both settings and `I18n`. When no settings JSON
exists, startup reads the old two-byte `language.bin` and persists a stable code
in `settings.json`; a malformed or unknown index leaves the Korean default.
An existing settings JSON takes precedence, including when it fails to parse,
so migration cannot overwrite a user's settings file.

The 1.6 keyboard layout table retains all nine layouts and their persisted mask
positions. Its language codes and display names no longer require every layout
language to be compiled into the firmware translation enum. The two-language
build therefore compiles without changing saved layout masks.

Before flashing hardware, run host tests, layout and golden tests, desktop
navigation and EPUB rendering, both firmware builds, and a partition-specific
size check. The desktop emulator cannot validate e-ink waveforms, electrical
touch behavior, SD timing, WiFi heap pressure, deep-sleep current, battery
hardware, USB electrical behavior, or the watchdog.
