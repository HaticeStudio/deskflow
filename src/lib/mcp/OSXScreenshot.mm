/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Chimera Kang
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXScreenshot.h"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <vector>

namespace deskflow::mcp {

namespace {

constexpr int64_t kCaptureTimeoutNs = 8LL * NSEC_PER_SEC;

std::vector<CGDirectDisplayID> activeDisplays()
{
  uint32_t count = 0;
  CGGetActiveDisplayList(0, nullptr, &count);
  std::vector<CGDirectDisplayID> ids(count);
  if (count > 0) {
    CGGetActiveDisplayList(count, ids.data(), &count);
    ids.resize(count);
  }
  return ids;
}

QByteArray encodePng(CGImageRef image)
{
  CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
  CGImageDestinationRef dest = CGImageDestinationCreateWithData(data, CFSTR("public.png"), 1, nullptr);
  QByteArray out;
  if (dest != nullptr) {
    CGImageDestinationAddImage(dest, image, nullptr);
    if (CGImageDestinationFinalize(dest)) {
      out = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), static_cast<int>(CFDataGetLength(data)));
    }
    CFRelease(dest);
  }
  CFRelease(data);
  return out;
}

//! Block until ScreenCaptureKit returns the shareable content, or time out.
SCShareableContent *shareableContent(QString &error)
{
  __block SCShareableContent *content = nil;
  __block NSString *errText = nil;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                             onScreenWindowsOnly:YES
                                               completionHandler:^(SCShareableContent *c, NSError *e) {
                                                 content = c;
                                                 errText = e ? e.localizedDescription : nil;
                                                 dispatch_semaphore_signal(sem);
                                               }];
  if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, kCaptureTimeoutNs)) != 0) {
    error = QStringLiteral("ScreenCaptureKit timed out listing displays");
    return nil;
  }
  if (content == nil) {
    error = QStringLiteral("ScreenCaptureKit: %1 (Screen Recording permission?)")
                .arg(QString::fromNSString(errText ?: @"no shareable content"));
    return nil;
  }
  return content;
}

//! Capture one display scaled to width*scale x height*scale.
CGImageRef captureDisplay(SCDisplay *display, double scale, QString &error)
{
  SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
  SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
  const CGFloat w = std::max<CGFloat>(1, display.width * scale);
  const CGFloat h = std::max<CGFloat>(1, display.height * scale);
  config.width = static_cast<size_t>(w);
  config.height = static_cast<size_t>(h);
  config.showsCursor = YES;
  config.captureResolution = SCCaptureResolutionBest;

  __block CGImageRef image = nullptr;
  __block NSString *errText = nil;
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  [SCScreenshotManager captureImageWithFilter:filter
                                configuration:config
                            completionHandler:^(CGImageRef img, NSError *e) {
                              if (img)
                                image = CGImageRetain(img);
                              errText = e ? e.localizedDescription : nil;
                              dispatch_semaphore_signal(sem);
                            }];
  if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, kCaptureTimeoutNs)) != 0) {
    error = QStringLiteral("ScreenCaptureKit timed out capturing display");
    return nullptr;
  }
  if (image == nullptr) {
    error = QStringLiteral("ScreenCaptureKit capture failed: %1")
                .arg(QString::fromNSString(errText ?: @"unknown error (Screen Recording permission?)"));
  }
  return image;
}

} // namespace

int displayCount()
{
  return static_cast<int>(activeDisplays().size());
}

ScreenshotResult captureDisplayPng(int index, double scale)
{
  ScreenshotResult result;
  scale = std::clamp(scale, 0.05, 1.0);

  @autoreleasepool {
    SCShareableContent *content = shareableContent(result.error);
    if (content == nil)
      return result;

    NSArray<SCDisplay *> *displays = content.displays;
    if (displays.count == 0) {
      result.error = QStringLiteral("no displays available to ScreenCaptureKit");
      return result;
    }
    // Index 0 is the main display (CGMainDisplayID), the rest in CoreGraphics order.
    const auto ids = activeDisplays();
    if (index < 0 || index >= static_cast<int>(ids.size())) {
      result.error = QStringLiteral("display index %1 out of range (0..%2)").arg(index).arg(ids.size() - 1);
      return result;
    }
    const CGDirectDisplayID wanted = (index == 0) ? CGMainDisplayID() : ids[static_cast<size_t>(index)];
    SCDisplay *target = nil;
    for (SCDisplay *d in displays) {
      if (d.displayID == wanted) {
        target = d;
        break;
      }
    }
    if (target == nil)
      target = displays.firstObject;

    CGImageRef image = captureDisplay(target, scale, result.error);
    if (image == nullptr)
      return result;

    result.width = static_cast<int>(CGImageGetWidth(image));
    result.height = static_cast<int>(CGImageGetHeight(image));
    result.png = encodePng(image);
    if (result.png.isEmpty())
      result.error = QStringLiteral("PNG encode failed");
    CGImageRelease(image);
  }
  return result;
}

} // namespace deskflow::mcp
