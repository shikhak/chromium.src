// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSER_PROCESS_PLATFORM_PART_BASE_H_
#define CHROME_BROWSER_BROWSER_PROCESS_PLATFORM_PART_BASE_H_

#include "chrome/browser/buildflags.h"

// A base class for platform-specific BrowserProcessPlatformPart
// implementations. This class itself should never be used verbatim.
class BrowserProcessPlatformPartBase {
 public:
  BrowserProcessPlatformPartBase();

  BrowserProcessPlatformPartBase(const BrowserProcessPlatformPartBase&) =
      delete;
  BrowserProcessPlatformPartBase& operator=(
      const BrowserProcessPlatformPartBase&) = delete;

  virtual ~BrowserProcessPlatformPartBase();

#if BUILDFLAG(ENABLE_PROCESS_SINGLETON)
  // Called after launch, whether it is from creating a new process or after
  // rendezvous to an existing process via the process singleton.
  virtual void OnBrowserLaunch();
#endif

  // Called in the middle of BrowserProcessImpl::StartTearDown().
  virtual void StartTearDown();

  // Called from AttemptExitInternal().
  virtual void AttemptExit(bool try_to_quit_application);

  // Called at the end of BrowserProcessImpl::PreMainMessageLoopRun().
  virtual void PreMainMessageLoopRun();
};

#endif  // CHROME_BROWSER_BROWSER_PROCESS_PLATFORM_PART_BASE_H_
