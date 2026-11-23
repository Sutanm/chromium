// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/ppapi_plugin_sandboxed_process_launcher_delegate.h"

#include <string>

#include "base/command_line.h"
#include "build/build_config.h"
#include "content/public/common/content_switches.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"

#if BUILDFLAG(IS_WIN)
#include "sandbox/policy/win/sandbox_win.h"
#include "sandbox/win/src/process_mitigations.h"
#include "sandbox/win/src/sandbox_policy.h"
#endif

namespace content {

PpapiPluginSandboxedProcessLauncherDelegate::
    PpapiPluginSandboxedProcessLauncherDelegate(bool is_flash)
    : is_flash_(is_flash) {}

#if BUILDFLAG(USE_ZYGOTE)
ZygoteCommunication* PpapiPluginSandboxedProcessLauncherDelegate::GetZygote() {
  const base::CommandLine& browser_command_line =
      *base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringType plugin_launcher =
      browser_command_line.GetSwitchValueNative(switches::kPpapiPluginLauncher);
  if (!plugin_launcher.empty())
    return nullptr;
  return GetGenericZygote();
}
#endif  // BUILDFLAG(USE_ZYGOTE)

sandbox::mojom::Sandbox
PpapiPluginSandboxedProcessLauncherDelegate::GetSandboxType() {
#if BUILDFLAG(IS_WIN)
  // Use Chromium's generic Windows sandbox with the Pepper-specific config
  // below. The standalone PPAPI sandbox type only exists on other platforms.
  return sandbox::mojom::Sandbox::kUtility;
#else
  return sandbox::mojom::Sandbox::kPpapi;
#endif
}

#if BUILDFLAG(IS_WIN)
std::string PpapiPluginSandboxedProcessLauncherDelegate::GetSandboxTag() {
  return sandbox::policy::SandboxWin::GetSandboxTagForDelegate(
      is_flash_ ? "ppapi-flash" : "ppapi", GetSandboxType());
}

bool PpapiPluginSandboxedProcessLauncherDelegate::InitializeConfig(
    sandbox::TargetConfig* config) {
  if (sandbox::policy::SandboxWin::AddWin32kLockdownPolicy(config) !=
      sandbox::SBOX_ALL_OK) {
    return false;
  }

  // Flash compiles ActionScript at runtime. Keep dynamic code disabled for
  // other Pepper plugins.
  if (is_flash_) {
    return true;
  }
  return config->SetDelayedProcessMitigations(
             config->GetDelayedProcessMitigations() |
             sandbox::MITIGATION_DYNAMIC_CODE_DISABLE) == sandbox::SBOX_ALL_OK;
}
#endif

#if BUILDFLAG(IS_MAC)
bool PpapiPluginSandboxedProcessLauncherDelegate::DisclaimResponsibility() {
  return true;
}
bool PpapiPluginSandboxedProcessLauncherDelegate::
    EnableCpuSecurityMitigations() {
  return true;
}
#endif

}  // namespace content
