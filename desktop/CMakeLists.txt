cmake_minimum_required(VERSION 3.20)
project(crosspoint_desktop LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_x4pro_geometry.py
    ${CMAKE_CURRENT_SOURCE_DIR}/../freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h
    ${CMAKE_CURRENT_BINARY_DIR}/DesktopBoardProfile.h
  COMMAND_ERROR_IS_FATAL ANY
)

add_executable(crosspoint_desktop
  DesktopPreview.cpp
  DesktopParserStubs.cpp
  DesktopEpub.cpp
  ../lib/GfxRenderer/GfxRenderer.cpp
  ../lib/GfxRenderer/FontCacheManager.cpp
  ../lib/GfxRenderer/Bitmap.cpp
  ../lib/GfxRenderer/BitmapHelpers.cpp
  ../lib/EpdFont/EpdFont.cpp
  ../lib/EpdFont/EpdFontFamily.cpp
  ../lib/EpdFont/FontDecompressor.cpp
  ../lib/EpdFont/SdCardFont.cpp
  ../lib/InflateReader/InflateReader.cpp
  ../lib/uzlib/src/tinflate.c
  UzlibChecksums.c
  ../lib/Utf8/Utf8.cpp
  ../lib/MiniBidi/BidiUtils.cpp
  ../lib/MiniBidi/minibidi.c
  ../lib/Memory/BuildScratch.cpp
  ../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ../lib/Epub/Epub/css/CssParser.cpp
  ../lib/Epub/Epub/ParsedText.cpp
  ../lib/Epub/Epub/blocks/TextBlock.cpp
  ../lib/Epub/Epub/Page.cpp
  ../lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ../lib/Epub/Epub/htmlEntities.cpp
  ../lib/Epub/Epub/hyphenation/HyphenationCommon.cpp
)
add_library(desktop_expat STATIC ../lib/expat/xmlparse.c ../lib/expat/xmlrole.c ../lib/expat/xmltok.c)
target_include_directories(desktop_expat PUBLIC ../lib/expat)
target_compile_definitions(desktop_expat PRIVATE XML_GE=0 XML_CONTEXT_BYTES=1024)
target_compile_definitions(desktop_expat PUBLIC XML_STATIC)
target_link_libraries(crosspoint_desktop PRIVATE desktop_expat)
add_library(desktop_miniz STATIC ../lib/miniz/third_party/miniz.c)
target_include_directories(desktop_miniz PUBLIC ../lib/miniz/third_party)
target_compile_definitions(desktop_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_link_libraries(crosspoint_desktop PRIVATE desktop_miniz)
enable_testing()
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/testdata/make_sample_epub.py
    ${CMAKE_CURRENT_BINARY_DIR}/korean-sample.epub
  COMMAND_ERROR_IS_FATAL ANY
)
add_test(NAME desktop_korean_xhtml
  COMMAND crosspoint_desktop --headless --xhtml ${CMAKE_CURRENT_SOURCE_DIR}/testdata/korean.xhtml
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/korean-layout.bmp)
add_test(NAME desktop_korean_epub
  COMMAND crosspoint_desktop --headless --book ${CMAKE_CURRENT_BINARY_DIR}/korean-sample.epub
    --screenshot ${CMAKE_CURRENT_BINARY_DIR}/korean-epub.bmp)
target_include_directories(crosspoint_desktop PRIVATE
  stubs
  ${CMAKE_CURRENT_BINARY_DIR}
  ../lib/GfxRenderer
  ../lib/EpdFont
  ../lib/InflateReader
  ../lib/uzlib/src
  ../lib/Utf8
  ../lib/MiniBidi
  ../lib/Memory
  ../lib/Logging
  ../lib
  ../lib/Epub
  ../lib/expat
  ../lib/XmlParserUtils
  ../lib/Serialization
)
if(MSVC)
  target_compile_definitions(crosspoint_desktop PRIVATE UNICODE _UNICODE XML_STATIC XML_GE=0 XML_CONTEXT_BYTES=1024 strcasecmp=_stricmp)
  target_compile_options(crosspoint_desktop PRIVATE /utf-8 /W3 /Zc:preprocessor)
  target_link_libraries(crosspoint_desktop PRIVATE user32 gdi32)
endif()
