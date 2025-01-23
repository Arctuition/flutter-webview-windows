#include "webview_platform.h"

#include <DispatcherQueue.h>
#include <shlobj.h>
#include <windows.graphics.capture.h>

#include <wrl.h>
#include <wrl/implements.h>
#include <windows.system.h>

#include <filesystem>
#include <iostream>

using namespace Microsoft::WRL;
using namespace ABI::Windows::System;

#pragma comment(lib, "runtimeobject.lib")

WebviewPlatform::WebviewPlatform()
    : rohelper_(std::make_unique<rx::RoHelper>(RO_INIT_SINGLETHREADED)) {
  if (rohelper_->WinRtAvailable()) {
    // When used together with flutter_inappwebview, this plugin will fail to initialize because a thread can only create one dispatcher_queue_controller, so here we prioritize getting the existing controller, and only create a new one if none exists.
    ComPtr<IDispatcherQueueStatics> dispatcher_statics;
    HSTRING class_name;
    if (SUCCEEDED(WindowsCreateString(
            RuntimeClass_Windows_System_DispatcherQueue,
            static_cast<UINT32>(wcslen(RuntimeClass_Windows_System_DispatcherQueue)),
            &class_name))) {
      if (SUCCEEDED(RoGetActivationFactory(
              class_name, IID_PPV_ARGS(&dispatcher_statics)))) {
        ComPtr<IDispatcherQueue> current_queue;
        if (FAILED(dispatcher_statics->GetForCurrentThread(current_queue.GetAddressOf()))) {
          DispatcherQueueOptions options{sizeof(DispatcherQueueOptions),
                                   DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
          if (FAILED(rohelper_->CreateDispatcherQueueController(
            options, dispatcher_queue_controller_.put()))) {
            std::cerr << "Creating DispatcherQueueController failed." << std::endl;
            WindowsDeleteString(class_name);
            return;
          }
        } else {
          current_queue->QueryInterface(IID_PPV_ARGS(dispatcher_queue_controller_.put()));
        }
      }
      WindowsDeleteString(class_name);
    }

    if (!IsGraphicsCaptureSessionSupported()) {
      std::cerr << "Windows::Graphics::Capture::GraphicsCaptureSession is not "
                   "supported."
                << std::endl;
      return;
    }

    graphics_context_ = std::make_unique<GraphicsContext>(rohelper_.get());
    valid_ = graphics_context_->IsValid();
  }
}

bool WebviewPlatform::IsGraphicsCaptureSessionSupported() {
  HSTRING className;
  HSTRING_HEADER classNameHeader;

  if (FAILED(rohelper_->GetStringReference(
          RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureSession,
          &className, &classNameHeader))) {
    return false;
  }

  ABI::Windows::Graphics::Capture::IGraphicsCaptureSessionStatics*
      capture_session_statics;
  if (FAILED(rohelper_->GetActivationFactory(
          className,
          __uuidof(
              ABI::Windows::Graphics::Capture::IGraphicsCaptureSessionStatics),
          (void**)&capture_session_statics))) {
    return false;
  }

  boolean is_supported = false;
  if (FAILED(capture_session_statics->IsSupported(&is_supported))) {
    return false;
  }

  return !!is_supported;
}

std::optional<std::wstring> WebviewPlatform::GetDefaultDataDirectory() {
  PWSTR path_tmp;
  if (!SUCCEEDED(
          SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path_tmp))) {
    return std::nullopt;
  }
  auto path = std::filesystem::path(path_tmp);
  CoTaskMemFree(path_tmp);

  wchar_t filename[MAX_PATH];
  GetModuleFileName(nullptr, filename, MAX_PATH);
  path /= "flutter_webview_windows";
  path /= std::filesystem::path(filename).stem();

  return path.wstring();
}
