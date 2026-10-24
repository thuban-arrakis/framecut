cmake_minimum_required(VERSION 3.22)
project(Framecut VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Multimedia Concurrent)
find_package(PkgConfig QUIET)
add_library(ffmpeg_pixels INTERFACE)
if(PkgConfig_FOUND)
  pkg_check_modules(FFMPEG_PIXELS REQUIRED IMPORTED_TARGET libswscale libavutil)
  target_link_libraries(ffmpeg_pixels INTERFACE PkgConfig::FFMPEG_PIXELS)
else()
  find_path(FFMPEG_INCLUDE_DIR libavutil/avutil.h HINTS /opt/homebrew/include REQUIRED)
  # Keep Homebrew headers and libraries together; CMAKE_PREFIX_PATH also contains
  # Qt's different FFmpeg build, whose ABI may not match these headers.
  find_library(SWSCALE_LIBRARY swscale PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH REQUIRED)
  find_library(AVUTIL_LIBRARY avutil PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH REQUIRED)
  target_include_directories(ffmpeg_pixels INTERFACE ${FFMPEG_INCLUDE_DIR})
  target_link_libraries(ffmpeg_pixels INTERFACE ${SWSCALE_LIBRARY} ${AVUTIL_LIBRARY})
endif()

add_library(framecut_media STATIC src/mediaengine.h src/mediaengine.cpp)
target_link_libraries(framecut_media PUBLIC Qt6::Core)
target_include_directories(framecut_media PUBLIC src)

add_library(framecut_ui STATIC
  src/appsettings.h src/appsettings.cpp src/theme.h src/theme.cpp
  src/controller.h src/controller.cpp
  src/mainwindow.h src/mainwindow.cpp
  src/previewwidget.h src/previewwidget.cpp
  src/timelinewidget.h src/timelinewidget.cpp)
target_link_libraries(framecut_ui PUBLIC framecut_media Qt6::Widgets Qt6::Multimedia Qt6::Concurrent ffmpeg_pixels)
target_include_directories(framecut_ui PUBLIC src)

qt_add_executable(Framecut MACOSX_BUNDLE src/main.cpp resources.qrc)
target_link_libraries(Framecut PRIVATE framecut_ui)
if(APPLE)
  set_source_files_properties(assets/icon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  target_sources(Framecut PRIVATE assets/icon.icns)
  set_target_properties(Framecut PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME Framecut
    MACOSX_BUNDLE_GUI_IDENTIFIER local.framecut.editor
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE icon.icns)
else()
  set_target_properties(Framecut PROPERTIES OUTPUT_NAME framecut)
endif()

install(TARGETS Framecut BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
  install(FILES packaging/framecut.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES assets/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/1024x1024/apps RENAME framecut.png)
endif()

if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(mediaengine_test tests/mediaengine_test.cpp)
  target_link_libraries(mediaengine_test PRIVATE framecut_media Qt6::Test)
  target_compile_definitions(mediaengine_test PRIVATE FRAMECUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME mediaengine COMMAND mediaengine_test)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_smoke.cpp")
    add_executable(ui_smoke tests/ui_smoke.cpp resources.qrc)
    target_link_libraries(ui_smoke PRIVATE framecut_ui Qt6::Test)
    target_compile_definitions(ui_smoke PRIVATE FRAMECUT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ui_smoke COMMAND ui_smoke)
    set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FFMPEG_DECODING_HW_DEVICE_TYPES=,;QT_DISABLE_HW_TEXTURES_CONVERSION=1;QT_MEDIA_BACKEND=ffmpeg")
  endif()
endif()

set(CPACK_PACKAGE_NAME framecut)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR Framecut)
set(CPACK_PACKAGE_CONTACT "Framecut administrators")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local desktop video editor with trimming and audio removal")
set(CPACK_DEBIAN_PACKAGE_SECTION video)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "ffmpeg, qt6-qpa-plugins, fonts-dejavu-core")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/release")
set(CPACK_GENERATOR DEB)
include(CPack)
