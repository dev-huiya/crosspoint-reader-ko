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
  (`GfxRenderer::drawTextTracked`); a paragraph's last line and the page's
  last line stay ragged. Paragraph indent is one U+3000; extra paragraph
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
  is read as well, so a card coming from 1.5 keeps its positions. A 1.6
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

