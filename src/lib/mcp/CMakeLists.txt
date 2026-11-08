# SPDX-FileCopyrightText: 2026 Chimera Kang
# SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception

set(lib_name mcp)

set(PLATFORM_CODE)
if(APPLE)
  set(PLATFORM_CODE
    OSXScreenshot.h
    OSXScreenshot.mm
  )
endif()

add_library(${lib_name} STATIC ${PLATFORM_CODE}
  McpBridge.cpp
  McpBridge.h
  McpStdioBridge.cpp
  McpStdioBridge.h
  McpServer.cpp
  McpServer.h
  McpTools.cpp
  McpTools.h
)

target_include_directories(${lib_name} PUBLIC ${CMAKE_SOURCE_DIR}/src/lib)

target_link_libraries(${lib_name}
  PUBLIC
    Qt6::Core
    Qt6::Network
  PRIVATE
    app
    base
    common
    platform
    server
)

if(APPLE)
  find_library(COREGRAPHICS_LIBRARY CoreGraphics)
  find_library(IMAGEIO_LIBRARY ImageIO)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  find_library(FOUNDATION_LIBRARY Foundation)
  find_library(SCREENCAPTUREKIT_LIBRARY ScreenCaptureKit)
  target_link_libraries(${lib_name} PRIVATE
    ${COREGRAPHICS_LIBRARY}
    ${IMAGEIO_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${FOUNDATION_LIBRARY}
    ${SCREENCAPTUREKIT_LIBRARY}
  )
  set_source_files_properties(OSXScreenshot.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()
