/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/mac/video_renderer_mac.h"

#import <Cocoa/Cocoa.h>

// Creates a Cocoa Window with an OpenGL context, used together with an OpenGL
// renderer.
@interface CocoaWindow : NSObject {
 @private
  NSWindow *window_;
#if WEBRTC_WEBKIT_BUILD
  NSOpenGLView *view_;
#else
  NSOpenGLContext *context_;
#endif
  NSString *title_;
  int width_;
  int height_;
}

- (id)initWithTitle:(NSString *)title width:(int)width height:(int)height;
// 'createWindow' must be called on the main thread.
- (void)createWindow:(NSObject *)ignored;
- (void)makeCurrentContext;

@end

@implementation CocoaWindow
static NSInteger nextXOrigin_;
static NSInteger nextYOrigin_;

- (id)initWithTitle:(NSString *)title width:(int)width height:(int)height {
  self = [super init];
  if (self) {
    title_ = title;
    width_ = width;
    height_ = height;
  }
  return self;
}

#if WEBRTC_WEBKIT_BUILD
- (void)dealloc {
  @autoreleasepool {
    [NSOpenGLContext clearCurrentContext];
    [view_ clearGLContext];
    [view_ removeFromSuperview];
    [window_ orderOut:NSApp];
  }
}
#endif

- (void)createWindow:(NSObject *)ignored {
#if WEBRTC_WEBKIT_BUILD
  @autoreleasepool {
#endif
  NSInteger xOrigin = nextXOrigin_;
  NSRect screenFrame = [[NSScreen mainScreen] frame];
  if (nextXOrigin_ + width_ < screenFrame.size.width) {
    nextXOrigin_ += width_;
  } else {
    xOrigin = 0;
    nextXOrigin_ = 0;
    nextYOrigin_ += height_;
  }
  if (nextYOrigin_ + height_ > screenFrame.size.height) {
    xOrigin = 0;
    nextXOrigin_ = 0;
    nextYOrigin_ = 0;
  }
  NSInteger yOrigin = nextYOrigin_;
  NSRect windowFrame = NSMakeRect(xOrigin, yOrigin, width_, height_);
  window_ = [[NSWindow alloc] initWithContentRect:windowFrame
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered
                                            defer:NO];

  NSRect viewFrame = NSMakeRect(0, 0, width_, height_);
#if WEBRTC_WEBKIT_BUILD
  view_ = [[NSOpenGLView alloc] initWithFrame:viewFrame pixelFormat:nil];

  [[window_ contentView] addSubview:view_];
#else
  NSOpenGLView *view = [[NSOpenGLView alloc] initWithFrame:viewFrame pixelFormat:nil];
  context_ = [view openGLContext];

  [[window_ contentView] addSubview:view];
#endif
  [window_ setTitle:title_];
  [window_ makeKeyAndOrderFront:NSApp];
#if WEBRTC_WEBKIT_BUILD
  } // @autoreleasepool
#endif
}

- (void)makeCurrentContext {
#if WEBRTC_WEBKIT_BUILD
  @autoreleasepool {
    [[view_ openGLContext] makeCurrentContext];
  }
#else
  [context_ makeCurrentContext];
#endif
}

@end

namespace webrtc {
namespace test {

VideoRenderer *VideoRenderer::CreatePlatformRenderer(const char *window_title,
                                                     size_t width,
                                                     size_t height) {
  MacRenderer *renderer = new MacRenderer();
  if (!renderer->Init(window_title, width, height)) {
    delete renderer;
    return NULL;
  }
  return renderer;
}

MacRenderer::MacRenderer() : window_(NULL) {}

MacRenderer::~MacRenderer() {
#if WEBRTC_WEBKIT_BUILD
  @autoreleasepool {
#endif
  GlRenderer::Destroy();
#if WEBRTC_WEBKIT_BUILD
  } // @autoreleasepool
#endif
}

bool MacRenderer::Init(const char* window_title, int width, int height) {
#if WEBRTC_WEBKIT_BUILD
  @autoreleasepool {
#endif
  window_ = [[CocoaWindow alloc]
      initWithTitle:[NSString stringWithUTF8String:window_title]
              width:width
             height:height];
  if (!window_) return false;
  [window_ performSelectorOnMainThread:@selector(createWindow:)
                            withObject:nil
                         waitUntilDone:YES];

  [window_ makeCurrentContext];
  GlRenderer::Init();
  GlRenderer::ResizeViewport(width, height);
  return true;
#if WEBRTC_WEBKIT_BUILD
  } // @autoreleasepool
#endif
}

void MacRenderer::OnFrame(const VideoFrame& frame) {
#if WEBRTC_WEBKIT_BUILD
  @autoreleasepool {
#endif
  [window_ makeCurrentContext];
  GlRenderer::OnFrame(frame);
#if WEBRTC_WEBKIT_BUILD
  } // @autoreleasepool
#endif
}

}  // namespace test
}  // namespace webrtc
