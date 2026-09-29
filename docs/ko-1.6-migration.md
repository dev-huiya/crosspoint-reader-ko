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
| X3/X4 raw-SD staged OTA | UPSTREAM_REPLACEMENT | KO 1.5 needed it because its post-build script patched the image's eFuse fields for the X3 stock bootloader and the running app's `esp_image_verify` then rejected that patched image. Upstream 1.6 no longer patches images and wraps `bootloader_common_check_efuse_blk_validity` (#1805), so `esp_ota_*` OTA works on X3/X4 with unpatched images. See "OTA" below. |
| X4 Pro raw-SD OTA | UPSTREAM_REPLACEMENT | The official 1.6 `esp_ota_*` path over wolfSSL. |
| KO release endpoint and version parser | KEEP, REIMPLEMENT | Update endpoint and parser without replacing 1.6 networking/device profiles. |
| X4 Pro hardware drivers | UPSTREAM_REPLACEMENT | Use upstream 1.6 board, touch, frontlight, Home key, USB and sleep HAL. |
| Legacy `TextSettingsActivity` removal | REVIEW | 1.6 reader flow determines whether any KO-specific screen is needed. |
| KO TXT reader (character wrap, justification, indent/spacing, menu, page jump, reading time) | KEEP, REIMPLEMENT | Port onto the 1.6 streaming byte-offset TXT reader; keep its page index cache and add the KO layout inputs to the cache key. See "Phase 4". |
| Legacy battery calibration | UPSTREAM_REPLACEMENT | Decided 2026-09-21: follow the 1.6 SDK's battery reading for every board. The KO 1.5 X4 offset table is not carried; a board-specific error is reported upstream instead. |

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
| Re-verified 2026-09-21 on `release/1.6.0-ko` (wip split into feature commits) | Host tests 181/181 (MSVC 19.44, CMake 3.29). `default`: `firmware.bin` 4,813,376 bytes, program flash 4,799,659 / 6,553,600 (73.2%). `x4pro`: `firmware.bin` 4,704,816 bytes, program flash 4,704,314 / 6,553,600 (71.8%). |

Firmware toolchains: riscv32-esp-elf-g++ and xtensa-esp-elf-g++ 14.2.0
(esp-14.2.0_20251107). The KO baseline worktree contains a host-only MSVC CMake
option workaround, so its build banner says `dirty`; the firmware source itself
is the KO tip commit.

PlatformIO 6.2.0 failed here with a SCons `FortranCommon` import error. The
pioarduino core 6.1.19 that CI uses works when installed with `uv` into a
Python 3.13 `.venv` (`PLATFORMIO_CORE_DIR` pointed at a repo-local
`.platformio`). Both firmware builds need `PYTHONIOENCODING=utf-8` in this
Windows Korean locale; otherwise generation fails while printing an Arabic
translation. Run `pio` from PowerShell or cmd, not Git Bash: ESP-IDF's
`idf_tools.py` refuses an MSYS environment.

## Branch layout

`release/1.6.0-ko` starts at the 1.6.0 tag and carries the port as feature
commits (host build fixes, localization, built-in fonts, `.epdfont`, layout,
reading statistics, OTA version compare, desktop preview, docs). The original
single `wip` commit remains on `codex/release-1.6.0-ko` for reference; the two
trees are identical at the docs commit.

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

Before flashing hardware, run the host tests (including the Korean layout
suites), both firmware builds, and a partition-specific size check.

The Windows desktop emulator that the work order describes was prototyped on
this branch (a Win32 host backend running the real ActivityManager, reader
and fonts; kept on `backup/desktop-emulator`) and then dropped by decision on
2026-09-21 to concentrate on the firmware itself. Its one lasting result is
the built-in font coverage fix below: rendering the Hanja fixture on the host
showed kana and KS X 1001 symbols vanishing from the page.

## OTA

KO 1.5 downloaded the firmware to the SD card and flashed it with raw
partition writes (`firmware_flash::flashFromSdPath`), bypassing
`esp_ota_end()`. The reason was specific to KO 1.5's build: its
`patch_firmware_image.py` rewrote `esp_app_desc_t`'s eFuse block-revision
fields so the X3 stock bootloader would accept the image, and the running
firmware's `esp_image_verify` rejected the patched image with bogus eFuse
errors. Upstream 1.6 fixed the same X3 problem the other way round: images
are not patched, and `platformio.ini` wraps
`bootloader_common_check_efuse_blk_validity` (src/platform/skip_efuse_blk_check.c,
upstream #1805) so the app-side check passes. Its `OtaUpdater` streams the
download through `esp_ota_write()` and verifies with `esp_ota_end()`.

Decision: keep the upstream OTA path for every board. The KO changes to OTA are
the release feed (crosspoint-reader-ko releases), the `-ko.N` version compare
(`src/network/KoReleaseVersion.h`), and the release workflow writing the tag
into `platformio.ini`. Not verified on X3/X4 hardware in this migration (only an
X4 Pro is available); the C3 path is unchanged upstream code.

## Firmware size (Phase 5 gate)

All release-relevant environments on `release/1.6.0-ko` at the Phase 5 commit,
program flash usage against the 6,553,600-byte app partition
(`partitions.csv`, the same table for every board):

| Environment | Program flash | Usage | `firmware.bin` |
| --- | --- | --- | --- |
| `default` | 4,809,561 | 73.4% | 4,823,280 |
| `gh_release` | 4,764,419 | 72.7% | 4,778,128 |
| `x4pro` | 4,713,086 | 71.9% | 4,713,600 |
| `x4pro-gh_release` | 4,674,230 | 71.3% | 4,674,736 |

After the TXT reader port (commit `f217d88b`, 2026-09-21): `default`
4,882,573 (74.5%), `x4pro` 4,785,314 (73.0%). The built-in font coverage
extension (kana, KS X 1001 symbols, fullwidth forms) accounts for most of the
growth since the Phase 5 row; the TXT port itself is about 10 KB.

Headroom is over 1.6 MB on every board, so `CP_HYPHENATION_LANGS=0` (KO 1.5's
hyphenation-table cut) is not reintroduced and the upstream hyphenation
languages stay available when character wrap is off.

## Phase records

### Phase 3 — layout

- Implemented: `characterWrap` / `paragraphIndent` reach the dictionary
  definition pages and the Text Settings preview (they were silently off
  there); parser-level soft-flush regression tests (320/750-token thresholds,
  chunked `characterData()`, mixed script, punctuation, indent-once, gap cap
  with an uncapped control); golden layout snapshots for four setting
  combinations.
- Changed files: `src/util/DictHtmlPages.cpp`, `src/activities/settings/TextSettingsPreview.*`,
  `test/chapter_html_slim_parser/{KoreanParserLayoutTest,KoreanLayoutGoldenTest}.cpp`,
  `test/chapter_html_slim_parser/golden/*.txt`, `ParserTestAccess.h`.
- Tests added: 13 parser cases + 4 golden cases. Tests passed: host suite 181 -> 198, all green.
- Known issues: goldens are approved from the first 1.6 KO implementation (see
  the file header for why KO 1.5 cannot be the baseline). Cache version stays
  46: the layout representation did not change in this phase.

### Phase 4 — reader and fonts

- Implemented: KoPub UI fallback survives SD font load/unload; legacy
  `/.crosspoint/fonts` root and stale `systemFontPath` clearing; "UI font"
  setting (Pretendard or an SD family) applied immediately; reset reading time
  on the EPUB menu (1.6 has no TXT/XTC menu, so those readers keep only chapter
  selection); sleep image selection store (1.5 file format) with a FUI list
  screen and a selection-aware random picker; 83 stale Korean strings removed.
- Upstream replacements verified in code: auto page turn, long-press page
  behaviour, back-short-to-file-browser all exist in 1.6 and are untouched.
- Firmware: `default` 4,809,533 bytes program flash after this phase.

#### Phase 4 (continued) — TXT reader

The 1.6 TXT reader is a streaming reader with a cached page index of byte
offsets, built in full before the first page is drawn; KO 1.5's TXT reader
navigated by byte offset with no whole-file pagination. The KO layout was
ported onto the 1.6 structure first, then the 1.5 offset navigation was
brought back because the up-front index made a 3 MB file take minutes to open:

- Navigation (`TxtReaderActivity`): the page on screen is a byte offset; its
  end (= the next page's start) falls out of rendering it. Back uses a
  bounded history stack, then the page index, then a forward scan from an
  estimated earlier position (KO 1.5's `findBackwardPageStart`). Percent
  jumps are byte-based; page jumps use the index where it has reached and
  bytes-per-page beyond. The first page is drawn as soon as the file opens.
- Page index (`TxtPageIndex.h`, renderer-free): built in the background from
  `loop()` under the render lock in 40 ms / 16-page ticks, the way the EPUB
  reader's deferred section build runs, with `skipLoopDelay()` keeping the CPU
  at full speed meanwhile. Page numbers are exact once the index has passed
  the reader's position and estimated from the indexed average (or the first
  rendered page) before that; the status bar marks the total as an estimate
  (`pageCountEstimated`) until the index completes.

- `src/activities/reader/TxtLineBreak.h`: renderer-free line breaking. With
  character wrap on, a line breaks on the last fitting character (binary search
  over UTF-8 boundaries, one advance measurement per probe); off, on the last
  fitting space with a character-break fallback for over-wide words. A page
  never starts on a UTF-8 continuation byte.
- Justified alignment spreads the slack between glyphs
  (`GfxRenderer::drawTextTracked`); a paragraph's last line stays ragged,
  but the page's last line is stretched when its paragraph continues on the
  next page (KO 1.5 left it ragged, which read as a glyph pushed to the next
  page). Paragraph indent is one U+3000; extra paragraph
  spacing is half a line above every paragraph but the page's first, so pages
  are filled by height instead of a fixed line count.
- `TxtReaderMenuActivity`: the EPUB menu screen cut down to text settings,
  night mode, go to percent, auto page turn, long-press page jump
  (off/10/20/50/100), rotation, screenshot, go home and reset reading time,
  with page/percent/reading time in the header. Opened by Confirm, the touch
  menu gesture or a Home-key hold set to "reader menu".
- Long-press page jump: holding a page button for `SKIP_HOLD_MS` jumps by the
  menu's step. With the long-press button setting off, page turns move to the
  release while a step is active (press-to-turn cannot measure a hold).
- Progress: `progress.bin` is page (2 bytes, the 1.6 layout) + 2 zero bytes +
  the byte offset (4 bytes); the offset is the position, the page number is
  for upstream builds. A KO 1.5 `TXTP` v2 file (41 bytes, offset at the end)
  is read as well, so a card coming from 1.5 keeps its positions. A saved
  offset is restored as is, not snapped back to its paragraph start (KO 1.5
  and the first 1.6 port did, which reopened a page that began mid-paragraph
  one page early, most visibly on wake); `moveToOffset` still aligns it to
  the index page that contains it. A 1.6
  four-byte file names only a page; the reader starts at the top and jumps
  there once the background index reaches it, unless the user has moved on.
- Index cache: `CACHE_VERSION` 5 (1.6 wrote 3, the first KO port 4), keyed
  additionally on viewport height, line height, the wrap/indent/spacing flags
  and the indent width, and holding a `complete` flag + `indexedEnd` so a
  partial index (saved every 200 pages, on exit and on a layout change)
  resumes where it stopped.
- Tests: `test/txt_line_break` (5 cases: no glyph lost, no UTF-8 split, no
  line over width, word-wrap fallback, codepoint count) and
  `test/txt_page_index` (7 cases: fresh/partial/complete coverage, estimates,
  page number never past the total).
- Battery calibration: not ported, see the inventory (follow the 1.6 SDK).

### Phase 5 — OTA, release, size, cache

- Implemented: `-ko.N` version channel with suffix tolerance
  (`1.6.0-ko.0-x4pro` is a development build of ko.0), base version
  `1.6.0-ko.0`, release workflow sets the version from the tag. OTA path:
  upstream (see "OTA"). Size gate: table above. Cache: `SECTION_FILE_VERSION`
  46 (> upstream 45 > KO 1.5's 42), so caches from every earlier build rebuild.


## Rebase onto upstream 1.6.5

`release/1.6.5-ko` starts at the upstream `1.6.5` tag
(`93e98bb7`, 94 upstream commits after `1.6.0`; `freeink-sdk` at `111fdcc7`)
and replays the 30 `release/1.6.0-ko` commits in order with their original
messages and author. No KO commit touched the submodule pointer. Two
`fix(port)` commits and a translation commit were added; this section is
the docs commit. `release/1.6.0-ko` is unchanged.

The sections above describe the 1.6.0 port and keep their 1.6.0 numbers
(sizes, cache version 46, host test count); the values below supersede them
on this branch.

### Per-commit conflict notes

| KO commit | Conflicts and resolution |
| --- | --- |
| build(host): MSVC host tests | Upstream rewrote the parser test (13 tests, `#define private public`). Converted to the friend access struct, which gained the accessors those tests need. `ParsedText` friend added to the upstream `WordStore` version. |
| feat(ko): localization | **Dropped KO piece:** upstream #3618 added `custom_i18n_builtin_langs`; the KO `--languages` option in `gen_i18n.py` is replaced by `custom_i18n_builtin_langs = en,korean` in `platformio.ini`. Upstream keeps every `Language` enum value, so the KO decoupling of `KeyboardLayoutSet` from the enum is no longer needed and was dropped (upstream's table, including the new Arabic layout, is used). `restoreLegacyKoLanguage()` runs before the new `timezones::applyToClock()`. |
| feat(font): KoPub/Pretendard built-ins | `fromJson` keeps the KOPUB default plus upstream's Home-button legacy migration. `getReaderLineCompression()` keeps the flat KO 1.00/1.20/1.40/1.60 steps; upstream's new SD-font scale (0.95/1.0/1.3/1.6) is not used. |
| feat(font): legacy `.epdfont` | Upstream #3646 (TrueType on PSRAM boards) rewrote the registry scan: the `.cpfont`-over-`.epdfont` duplicate rule now works on `cpfontFiles`, and a loose root `.epdfont` is checked before the vector-font branch. Upstream #3633 replaced the per-pixel glyph loop with `drawGlyphBitmap()`; synthetic bold is now a second `drawGlyphBitmap()` offset one pixel along the glyph advance axis. |
| feat(layout): character wrap + indent | **Behaviour decision:** upstream #3700 stopped splitting Hangul into per-syllable tokens (Korean wraps at spaces unless hyphenation is on). KO character wrap disables hyphenation, so it would have silently become wrap-at-spaces. `hasCjkBreakOpportunityBetween()` / `cjkCharacterBreakByteOffsets()` now take `splitHangul = characterWrap`: on, the pre-#3700 per-syllable breaks (KO 1.5 / 1.6.0-ko behaviour, gap stretch still capped at half a space); off, upstream 1.6.5 behaviour. Section header keeps upstream's character/word spacing fields followed by the KO flags; `SECTION_FILE_VERSION` 49 (upstream 48). Text Settings layout rows: upstream word/character spacing plus the KO indent/wrap rows. |
| feat(reader): reading statistics | Menu construction uses upstream `chapterPosition()`; the reading timer starts after `loadBook()`, while upstream's `rememberBookOnceRendered()` (#3724) records the book after the first render. |
| feat(ota) / build(release) | Version `1.6.5-ko.0`. The upstream 1.6.5 release workflow runs on a published release and attaches `crosspoint-<tag>-<device>.bin`, the names the 1.6.5 `OtaUpdater` requests; its "tag must equal `platformio.ini`" check is replaced by the KO set-version-from-tag step. The workflow also attaches `firmware.bin` / `firmware-<board>.bin` copies, the names a 1.6.0-ko device requests, so those devices can still update. |
| style: clang-format | Conflicting hunks resolved to the merged content. |
| test: soft flush, golden snapshots | Upstream links the real `TextBlock`/`Page` into the parser test, so lines are captured from ParsedText callbacks or completed pages (`LayoutCapture.h`) instead of a stubbed `TextBlock` constructor. The stub renderer now measures 8 px per codepoint (not per byte), so the indent constant is 8 and **all four goldens were regenerated** on 1.6.5 (checked: no text dropped, indent once per paragraph; the `upstream` profile shows #3700's whole-word Hangul). A raw CR character literal became `'\r'`. |
| feat(sleep): sleep image selection | Upstream's About action and the KO sleep image action both kept. |
| feat(gfx): letter spacing | Upstream `drawText()` gained `tracking` (#3528; no tracking beside a space). The TXT justification needs spacing after every glyph including spaces, so `drawTextTracked()` stays; both go through a private `drawTextImpl()`. |
| feat(reader): TXT port / byte offsets | KO navigation kept; upstream's page-load error screen and `markPageRendered()` (#3724) added around it. |
| fix(port) commits (new) | Host tests: bundled Expat for `content_opf_parser`, a force-included `__attribute__` shim for MSVC, `std::filesystem::path` → `string()`, `wordAt` via the access struct, `size()` on the SD font test's `HalFile` stub, per-test temp files for parallel ctest. Firmware: the new slider popup uses `UI_12_FONT_ID` (no Noto Sans 18 in the KO profile); the TXT reader opens its menu on the Home action mapped to "reader menu" (upstream removed `wasHomeKeyHold()` and the long-press menu setting). |
| other commits | Applied without conflicts. The desktop preview is still added and then dropped by its later commit. |

### Results

| Check | Result |
| --- | --- |
| Host tests (MSVC 19.4x, CMake 4.4, `ctest -j 8`) | 407/407 pass. |
| `default` firmware (pioarduino core 6.1.19, platform 55.03.311) | Program flash 5,652,669 / 6,553,600 (86.3%), RAM 58,288 / 327,680 (17.8%), `firmware.bin` 5,666,736 bytes (branch tip). |
| `x4pro` firmware | Program flash 5,690,842 / 6,553,600 (86.8%), RAM 101,736 / 327,680 (31.0%), `firmware.bin` 5,695,856 bytes (before the translation commit, about +1 KB after it). |
| Leftover conflict markers | None. |

The platform install upgrades `.platformio/penv` to PlatformIO 6.2.0, whose
nested ESP-IDF build fails with the SCons `FortranCommon` import error; pin
`pioarduino==6.1.19` in that penv, as `release.yml` does.

Flash headroom fell from about 1.0 MB to about 0.9 MB (x4pro) since the
1.6.0-ko tip; the uncompressed Pretendard UI font (+0.6 MB) is the largest
item since the Phase 5 table.

### Not verified

- No device run. Korean character wrap, justified gaps and paragraph indent
  on a real page; synthetic bold of a KO 1.5 `.epdfont`; the slider popup
  readout in Pretendard; the TXT reader menu on the Home key; OTA from
  1.6.0-ko to a 1.6.5-ko release (legacy asset names) and between 1.6.5-ko
  releases (new names).
- Only `default` and `x4pro` were built; `sticky`, `x4c` and `papermono`
  were not.
- Host tests and the `x4pro` build predate the translation commit, which
  changes only `korean.yaml`; `default` was rebuilt at the tip.

## Configurable controls port and sibling covers

The configurable reader controls (`0dff35ce`, per-button short/long/double
press bindings, reader touch-zone presets, cached TXT covers) were only on
`release/1.6.0-ko-1`. They are ported onto this branch with the original
author and without the `1.6.0-ko.1` version bump, followed by the
integration with 1.6.5's own controls. The Optimized home theme
(`817a6b6a`) is reverted; a stored `uiTheme` 5 is clamped to the default
theme by the settings loader.

### Overlap with 1.6.5 controls

The per-button bindings (Settings → Controls → a button → short / long /
double press) are the single configuration for every physical button:

| 1.6.5 feature | Resolution |
| --- | --- |
| Home Button Gestures (#3516): Home key tap / double tap / long press actions, `HomeButtonSettingsActivity`, `homeButtonAction()` | Merged. The Home key goes through the bindings' press classifier; its three actions become the Home button's default short / double / long bindings. The Home Button Gestures screen and `homeButtonAction()` are removed. Reader menu, bookmark, dictionary, sync, footnotes and rotate reach the EPUB reader, TXT reader and image viewer through `handleButtonAction()`; the EPUB reader keeps upstream's toolbar menu, bookmark message, footnote return and automatic page turn stop. Transfer loops (`update(true)`) defer bound command actions as upstream deferred Home actions. |
| X4 Pro power double click toggles the frontlight (#3089, `doubleClickPwrLight`) | Merged into the default power double-press binding (Toggle light, only when the setting was on). The upstream click-window code stays behind `!buttonBindingsReady`. |
| Short power button / long-press page button / Confirm long-press menu / side layout (incl. #3586's NEXT_NEXT, PREV_PREV) / front buttons follow orientation | Folded into the default bindings, then hidden from the device and web settings (`isReplacedByButtonBindings()`); they stay in `settings.json` as the defaults source. |
| Per-direction page-turn gestures (#3586) and the reader touch-zone presets | Both kept, in the Reader category: the gestures decide which directions accept taps and swipes, the preset decides where a tap lands and where the menu zone is. RTL books (#3709) reverse the left/right presets only. |
| Tilt page turn, the reader's Back short/long swap | Not button bindings; they stay under Controls after the button list. |

Defaults for a settings file without `buttonBindings` (fresh cards and
upgrades from 1.6.5-ko), with upstream's default settings:

| Button | Short | Long | Double |
| --- | --- | --- | --- |
| Back | Back | — (each screen's own hold: file browser / Home in the reader) | — |
| Confirm | Confirm | Confirm long-press menu setting (default none) | — |
| Left / Right (front) | Previous / next page | Chapter skip or rotate per the long-press setting (default none) | — |
| Upper / lower side | Previous / next page per the side layout | as front | — |
| Power | Short power setting (default none; Confirm on X4 Pro and shared Confirm/Power boards, Sleep on Paper Mono) | Sleep | Toggle light on X4 Pro |
| Home (X4 Pro) | Home | Reader menu | Toggle light |

A button with a double-press binding delays its short press by the
double-press window (500 ms; upstream's Home key used 350 ms). A hold
(700 ms) on a button without a long-press binding is released as a short
press, and screens' own holds (`wasLongPressed()`: the reader's Back to
the file browser, keyboard and list holds) and `isPressed()` auto-repeat
keep working for such buttons; a bound long press takes precedence. A
settings file from 1.6.0-ko.1 keeps its saved bindings.

Other changes: page turns under bindings read the page and direction
actions directly, so an inverted orientation no longer fires both
directions from one front button; one power click wakes the device when
the power short press is bound to Sleep; the TXT long-press page jump
fires on an unbound page-button hold or a chapter-skip binding. The
Controls list names the buttons by position. On the X4 Pro, whose only
page keys are the two side keys left and right of the screen (no front
left/right keys), they read 왼쪽 버튼 / 오른쪽 버튼 (`STR_BUTTON_SIDE_LEFT`
/ `_RIGHT`); other boards keep 위쪽 / 아래쪽 측면 버튼. Boards with front
left/right keys as well (X4, X3, X4 Classic, De-Link) would show two
identical rows with the X4 Pro names, which is why those keep the
side-button names.

### Sibling cover images

A same-name image beside a book (`foo.jpg|jpeg|png|bmp` for `foo.epub`,
`.xtc` or `.txt`, extension case ignored) is its cover wherever the book's
cover and thumbnails come from: the Cover Grid and the other home themes,
the sleep screen, the TXT cover. EPUB and XTC fall back to their own
cover; TXT books have only this one. `lib/SiblingCover` does the lookup
(one directory pass, nothing retained) and the conversion (the existing
JPEG/PNG converters; a BMP is downscaled to the same cover-filling size).
The book's cache directory records the result as `sibling.src` (`v2`,
size and path) or `cover.missing`. Opening the book rescans the folder
and, when the image was added, removed, renamed or resized, deletes the
cached `cover*.bmp` / `thumb_*.bmp` so they are rebuilt. An edit that
keeps the file size needs a cache clear. The Cover Grid also shows TXT
books' sibling covers.

Zoomed covers on the grid: a thumbnail is drawn 1:1, centered and clipped
to its slot, so it has to be built at the slot's size. A BMP sibling image
was copied unchanged, and the 1.6.0-ko.1 TXT code copied the full-screen
`cover.bmp` as its thumbnail; both showed a clipped, enlarged part of the
cover. BMPs are now downscaled, markers of the old format (no `v2`) count
as no lookup so the next open rebuilds the covers, and a thumbnail more
than 1.25 times its slot on both sides is drawn fitted to the slot
(`BaseTheme::drawCoverThumbFill`) until then.

### Home never builds covers

The Cover Grid home built every missing thumbnail synchronously inside its
second render, one book after another under a loading popup: for an EPUB
that is opening the zip, parsing content.opf, extracting the cover and a
JPEG/PNG decode (typically one to a few seconds each); an XTC was loaded
in full; a TXT or a first sibling lookup scanned the book's folder. A
conversion that failed (JPEG over 2048x3072, low heap, a bad PNG) left no
file and was retried on every home visit. With the index missing, the
grid also built the whole library index (a full card scan) before its
first frame. Device timings were not taken; `LOG_DBG` lines now report
the library read, cover path and draw times and each cover's paint time
(`HOME`), and each background build (`COVJOB`).

Now the home screen (every theme) only draws cached thumbnails and a
placeholder for a missing one, and records the thumbnail height it uses
in `state.json` (`homeCoverThumbHeight`). Once the reader has drawn a
book's first page, `util/CoverThumbJob` builds that book's cache in a
priority-0 FreeRTOS task (1.5 s start delay, 10 KB stack freed with the
task, one book at a time, skipped when the largest free block is under
48 KB): the sibling recheck, the home thumbnail at the recorded height and
for TXT the sleep screen's `cover.bmp`. The TXT reader no longer converts
its cover while opening. JPEG conversions take a mutex, since the
converter keeps file-level state. Deep sleep and USB drive mode pause the
job and wait up to 30 s for a running build to finish its file (USB mode
does not start otherwise). The framebuffer lent during a chapter build
(`buildscratch`) can only be claimed by the lending task, so the job's
inflate never takes it; a thumbnail shorter than its pixel rows (still
being written) draws as a placeholder. A missing library
index is left to the Library screen; the grid then shows recent books
only.

So on the first home visit after this change, books without a cached
thumbnail show the placeholder; opening a book builds its thumbnail and
the next home visit shows it. Books the grid takes from the library that
were never opened keep the placeholder, and so do recent books after a
theme change (a different thumbnail height) until they are opened again.
