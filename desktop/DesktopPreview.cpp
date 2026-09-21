#define NOMINMAX
#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/kopub_14_regular.h>
#include <builtinFonts/pretendard_10_regular.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "DesktopEpub.h"

namespace {
HalDisplay display;
std::vector<uint32_t> pixels;
int screenWidth = 0;
int screenHeight = 0;
GfxRenderer* activeRenderer = nullptr;
std::vector<std::unique_ptr<Page>> pages;
size_t currentPage = 0;
size_t currentChapter = 0;
DesktopEpub activeBook;
bool hasBook = false;
void copyFrameBuffer(const GfxRenderer& renderer);

void renderDocumentPage() {
  if (!activeRenderer || pages.empty()) return;
  activeRenderer->clearScreen();
  pages[currentPage]->render(*activeRenderer, 1, 24, 24);
  activeRenderer->displayBuffer();
  copyFrameBuffer(*activeRenderer);
}

void copyFrameBuffer(const GfxRenderer& renderer) {
  screenWidth = renderer.getScreenWidth();
  screenHeight = renderer.getScreenHeight();
  pixels.resize(static_cast<size_t>(screenWidth) * screenHeight);
  const uint8_t* frame = display.getFrameBuffer();
  const int widthBytes = display.getDisplayWidthBytes();
  for (int y = 0; y < screenHeight; ++y) {
    for (int x = 0; x < screenWidth; ++x) {
      int physicalX = x;
      int physicalY = y;
      switch (renderer.getOrientation()) {
        case GfxRenderer::Portrait:
          physicalX = y;
          physicalY = display.getDisplayHeight() - 1 - x;
          break;
        case GfxRenderer::LandscapeClockwise:
          physicalX = display.getDisplayWidth() - 1 - x;
          physicalY = display.getDisplayHeight() - 1 - y;
          break;
        case GfxRenderer::PortraitInverted:
          physicalX = display.getDisplayWidth() - 1 - y;
          physicalY = x;
          break;
        case GfxRenderer::LandscapeCounterClockwise:
          break;
      }
      const bool white = (frame[physicalY * widthBytes + physicalX / 8] & (0x80u >> (physicalX % 8))) != 0;
      pixels[static_cast<size_t>(y) * screenWidth + x] = white ? 0x00ffffffu : 0u;
    }
  }
}

bool renderXhtml(const std::string& path, GfxRenderer& renderer) {
  pages.clear();
  currentPage = 0;
  ChapterHtmlSlimParser parser(
      nullptr, path, renderer, 1, 1.2f, false, 0, static_cast<uint16_t>(renderer.getScreenWidth() - 48),
      static_cast<uint16_t>(renderer.getScreenHeight() - 72), false, false,
      [](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) { pages.push_back(std::move(page)); }, false, "", "",
      0, {}, nullptr, nullptr, true, true);
  if (!parser.parseAndBuildPages() || pages.empty()) return false;
  renderDocumentPage();
  return true;
}

bool loadChapter(size_t chapter) {
  if (!hasBook || !activeRenderer || chapter >= activeBook.chapterCount()) return false;
  const auto path = std::filesystem::temp_directory_path() /
                    ("crosspoint-desktop-" + std::to_string(GetCurrentProcessId()) + ".xhtml");
  const std::string filename = path.string();
  if (!activeBook.extractChapter(chapter, filename.c_str())) return false;
  const bool ok = renderXhtml(filename, *activeRenderer);
  std::filesystem::remove(path);
  if (ok) {
    currentChapter = chapter;
    std::printf("Chapter %zu/%zu, pages %zu\n", currentChapter + 1, activeBook.chapterCount(), pages.size());
  }
  return ok;
}

bool saveBmp(const char* path) {
  FILE* file = nullptr;
  if (fopen_s(&file, path, "wb") != 0) return false;
  const uint32_t imageSize = static_cast<uint32_t>(pixels.size() * sizeof(uint32_t));
  BITMAPFILEHEADER fileHeader{};
  fileHeader.bfType = 0x4d42;
  fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  fileHeader.bfSize = fileHeader.bfOffBits + imageSize;
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(info);
  info.biWidth = screenWidth;
  info.biHeight = -screenHeight;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  info.biSizeImage = imageSize;
  const bool ok = fwrite(&fileHeader, sizeof(fileHeader), 1, file) == 1 && fwrite(&info, sizeof(info), 1, file) == 1 &&
                  fwrite(pixels.data(), imageSize, 1, file) == 1;
  fclose(file);
  return ok;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_KEYDOWN) {
    if (wParam == VK_F12) saveBmp("desktop-screenshot.bmp");
    if (!pages.empty() && (wParam == VK_NEXT || wParam == VK_RIGHT || wParam == VK_DOWN)) {
      if (currentPage + 1 < pages.size()) {
        ++currentPage;
        renderDocumentPage();
      } else if (!hasBook || !loadChapter(currentChapter + 1)) {
        renderDocumentPage();
      }
      InvalidateRect(window, nullptr, FALSE);
    } else if (!pages.empty() && (wParam == VK_PRIOR || wParam == VK_LEFT || wParam == VK_UP)) {
      if (currentPage > 0) {
        --currentPage;
        renderDocumentPage();
      } else if (hasBook && currentChapter > 0 && loadChapter(currentChapter - 1)) {
        currentPage = pages.size() - 1;
        renderDocumentPage();
      }
      InvalidateRect(window, nullptr, FALSE);
    }
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = screenWidth;
    info.bmiHeader.biHeight = -screenHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    const double scale =
        std::min(static_cast<double>(client.right) / screenWidth, static_cast<double>(client.bottom) / screenHeight);
    const int drawnWidth = static_cast<int>(screenWidth * scale);
    const int drawnHeight = static_cast<int>(screenHeight * scale);
    const int left = (client.right - drawnWidth) / 2;
    const int top = (client.bottom - drawnHeight) / 2;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, left, top, drawnWidth, drawnHeight, 0, 0, screenWidth, screenHeight, pixels.data(), &info,
                  DIB_RGB_COLORS, SRCCOPY);
    EndPaint(window, &paint);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, wParam, lParam);
}
}  // namespace

int main(int argc, char** argv) {
  const char* screenshot = nullptr;
  const char* xhtml = nullptr;
  const char* book = nullptr;
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--headless") == 0)
      headless = true;
    else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
      screenshot = argv[++i];
    else if (strcmp(argv[i], "--xhtml") == 0 && i + 1 < argc)
      xhtml = argv[++i];
    else if (strcmp(argv[i], "--book") == 0 && i + 1 < argc)
      book = argv[++i];
  }
  display.begin();
  GfxRenderer renderer(display);
  activeRenderer = &renderer;
  renderer.begin();
  FontDecompressor decompressor;
  if (!decompressor.init()) return 1;
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  EpdFont body(&kopub_14_regular);
  EpdFont ui(&pretendard_10_regular);
  renderer.insertFont(1, EpdFontFamily(&body));
  renderer.insertFont(2, EpdFontFamily(&ui));
  if (book) {
    hasBook = activeBook.open(book);
    if (!hasBook || !loadChapter(0)) {
      std::fprintf(stderr, "Failed to open EPUB: %s\n", book);
      return 6;
    }
  } else if (xhtml) {
    if (!renderXhtml(xhtml, renderer)) {
      std::fprintf(stderr, "Failed to lay out XHTML: %s\n", xhtml);
      return 5;
    }
    std::printf("Rendered %zu pages from %s\n", pages.size(), xhtml);
  } else {
    renderer.clearScreen();
    renderer.drawText(1, 24, 24, "한글 레이아웃 미리보기", true);
    renderer.drawText(2, 24, 64, "CrossPoint 1.6 KO", true);
    renderer.displayBuffer();
    copyFrameBuffer(renderer);
  }
  if (screenshot && !saveBmp(screenshot)) {
    std::fprintf(stderr, "Failed to save screenshot: %s\n", screenshot);
    return 2;
  }
  if (headless) return 0;

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSW windowClass{};
  windowClass.lpfnWndProc = windowProc;
  windowClass.hInstance = instance;
  windowClass.lpszClassName = L"CrossPointDesktopPreview";
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&windowClass)) return 3;
  const HWND window =
      CreateWindowExW(0, windowClass.lpszClassName, L"CrossPoint 1.6 KO Desktop Preview", WS_OVERLAPPEDWINDOW,
                      CW_USEDEFAULT, CW_USEDEFAULT, 600, 920, nullptr, nullptr, instance, nullptr);
  if (!window) return 4;
  ShowWindow(window, SW_SHOW);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}
