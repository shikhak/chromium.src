// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include "ui/display/screen.h"

#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "chrome/browser/ui/views/frame/browser_frame.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/native_browser_frame_factory.h"
#include "chrome/grit/branded_strings.h"
#include "components/safe_browsing/core/browser/password_protection/metrics_util.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/widget/widget.h"

#if defined(USE_AURA)
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/window.h"
#include "ui/aura/window_occlusion_tracker.h"
#endif

#if BUILDFLAG(IS_CHROMEOS)
#include "chromeos/components/kiosk/kiosk_utils.h"
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "chrome/browser/ui/views/frame/custom_tab_browser_frame.h"
#endif

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/ui/views/frame/browser_frame_lacros.h"
#endif

#if BUILDFLAG(IS_MAC)
#include "chrome/common/chrome_features.h"
#endif

// static
BrowserWindow* BrowserWindow::CreateBrowserWindow(
    std::unique_ptr<Browser> browser,
    bool user_gesture,
    bool in_tab_dragging) {
#if defined(USE_AURA)
  // Avoid generating too many occlusion tracking calculation events before this
  // function returns. The occlusion status will be computed only once once this
  // function returns.
  // See crbug.com/1183894#c4
  aura::WindowOcclusionTracker::ScopedPause pause_occlusion;
#endif
  // Create the view and the frame. The frame will attach itself via the view
  // so we don't need to do anything with the pointer.
  bool frameless = browser->is_frameless();
  std::string position = browser->initial_position();
  BrowserView* view = new BrowserView(std::move(browser));
  BrowserFrame* browser_frame = nullptr;
#if BUILDFLAG(IS_CHROMEOS_ASH)
  if (view->browser()->is_type_custom_tab())
    browser_frame = new CustomTabBrowserFrame(view);
#endif
#if BUILDFLAG(IS_CHROMEOS_LACROS)
  browser_frame = new BrowserFrameLacros(view);
#endif
  if (!browser_frame)
    browser_frame = new BrowserFrame(view, frameless);
  if (in_tab_dragging)
    browser_frame->SetTabDragKind(TabDragKind::kAllTabs);
  bool got_saved_bounds = browser_frame->InitBrowserFrame();

  if (position == "mouse" && !got_saved_bounds) {
      gfx::Point cursor_pos(display::Screen::GetScreen()->GetCursorScreenPoint());
      gfx::Rect bounds = browser_frame->GetWindowBoundsInScreen();
      bounds.set_origin(cursor_pos);
      browser_frame->SetBounds(bounds);
  }
  if (position == "center" && !got_saved_bounds) {
    gfx::Rect bounds = browser_frame->GetWindowBoundsInScreen();
    browser_frame->CenterWindow(bounds.size());
  }

#if BUILDFLAG(IS_MAC)
  if (view->UsesImmersiveFullscreenMode()) {
    // This needs to happen after BrowserFrame has been initialized. It creates
    // a new Widget that copies the theme from BrowserFrame.
    view->CreateMacOverlayView();
  }
#endif  // IS_MAC

  if (view->GetIsPictureInPictureType() && view->GetLockAspectRatio()) {
    gfx::SizeF aspect_ratio(view->GetInitialAspectRatio(), 1.0f);
    view->GetWidget()->SetAspectRatio(aspect_ratio);
  }

#if defined(USE_AURA)
  // For now, all browser windows are true. This only works when USE_AURA
  // because it requires gfx::NativeWindow to be an aura::Window*.
  view->GetWidget()->GetNativeWindow()->SetProperty(
      aura::client::kCreatedByUserGesture, user_gesture);
#endif
#if BUILDFLAG(IS_CHROMEOS)
  if (chromeos::IsKioskSession()) {
    view->SetForceFullscreen(true);
  }
#endif

  return view;
}
