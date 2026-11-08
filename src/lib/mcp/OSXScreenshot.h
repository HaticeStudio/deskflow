/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QString>

namespace deskflow::mcp {

struct ScreenshotResult
{
  QByteArray png;
  int width = 0;
  int height = 0;
  QString error;
};

//! Number of active displays.
int displayCount();

//! Capture display \p index (0 = main) scaled by \p scale (0 < scale <= 1) as PNG.
/*!
Requires the macOS Screen Recording permission for this process; without it the
image is black or the call fails and \c error is set.
*/
ScreenshotResult captureDisplayPng(int index, double scale);

} // namespace deskflow::mcp
