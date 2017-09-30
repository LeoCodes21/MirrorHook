#include "stdafx.h"
#include "Memory.h"

#include <d3d9.h>
#include "D3D9Types.h"

#include <dinput.h>
#include "DI8Types.h"

#include "Definitions.hpp"
using MirrorHook::D3D9::D3D9Extender;
using MirrorHook::DirectInput8::DirectInput8Device;
using MirrorHook::DirectInput8::DirectInput8Extender;

#include "VTableHook.hpp"
#include <memory>
using std::unique_ptr;
using std::make_unique;

#include <map>
using std::map;

#include <vector>
using std::vector;

namespace D3D9Extenders {
   LPDIRECT3DDEVICE9      d3dDevice             = nullptr;

   vector<Reset_t>        vBeforeResetExtenders = vector<Reset_t>();
   vector<Reset_t>        vAfterResetExtenders  = vector<Reset_t>();
   vector<BeginScene_t>   vBeginSceneExtenders  = vector<BeginScene_t>();
   vector<EndScene_t>     vEndSceneExtenders    = vector<EndScene_t>();

   bool                   isExtenderReady       = false;

#pragma region function hooks
   unique_ptr<VTableHook> d3dDeviceHook         = nullptr;
   Reset_t                origReset             = nullptr;
   BeginScene_t           origBeginScene        = nullptr;
   EndScene_t             origEndScene          = nullptr;
   BeginStateBlock_t      origBeginStateBlock   = nullptr;

   HRESULT WINAPI hkBeginScene(LPDIRECT3DDEVICE9 pDevice) {
      if (!vBeginSceneExtenders.empty()) {
         for (BeginScene_t beginSceneExtender : vBeginSceneExtenders) {
            if (beginSceneExtender)
               beginSceneExtender(pDevice);
         }
      }
      return origBeginScene(pDevice);
   }
   HRESULT WINAPI hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
      if (pDevice->TestCooperativeLevel() == D3D_OK
          && !vEndSceneExtenders.empty()) {
         for (EndScene_t endSceneExtender : vEndSceneExtenders) {
            if (endSceneExtender)
               endSceneExtender(pDevice);
         }
      }

      return origEndScene(pDevice);
   }
   HRESULT WINAPI hkReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
      if (!vBeforeResetExtenders.empty()) {
         for (Reset_t beforeResetExtender : vBeforeResetExtenders) {
            if (beforeResetExtender)
               beforeResetExtender(pDevice, pPresentationParameters);
         }
      }

      auto retOrigReset = origReset(pDevice, pPresentationParameters);

      if (!vAfterResetExtenders.empty()) {
         for (Reset_t afterResetExtender : vAfterResetExtenders) {
            if (afterResetExtender)
               afterResetExtender(pDevice, pPresentationParameters);
         }
      }

      return retOrigReset;
   }
   HRESULT WINAPI hkBeginStateBlock(LPDIRECT3DDEVICE9 pDevice) {
      d3dDeviceHook->UnhookAll();

      auto retBeginStateBlock = origBeginStateBlock(pDevice);

      origReset           = d3dDeviceHook->Hook(16, hkReset);
      origBeginScene      = d3dDeviceHook->Hook(41, hkBeginScene);
      origEndScene        = d3dDeviceHook->Hook(42, hkEndScene);
      origBeginStateBlock = d3dDeviceHook->Hook(60, hkBeginStateBlock);
      return retBeginStateBlock;
   }
#pragma endregion

#pragma region exported helpers
   HRESULT WINAPI AddD3D9Extender(D3D9Extender extenderType, LPVOID extenderAddress) {
   #pragma ExportedFunction
      if (!isExtenderReady)
         return FALSE;

      switch (extenderType) {
         case D3D9Extender::BeforeReset:
            vBeforeResetExtenders.push_back(reinterpret_cast<Reset_t>(extenderAddress));
            break;
         case D3D9Extender::AfterReset:
            vAfterResetExtenders.push_back(reinterpret_cast<Reset_t>(extenderAddress));
            break;
         case D3D9Extender::BeginScene:
            vBeginSceneExtenders.push_back(reinterpret_cast<BeginScene_t>(extenderAddress));
            break;
         case D3D9Extender::EndScene:
            vEndSceneExtenders.push_back(reinterpret_cast<EndScene_t>(extenderAddress));
            break;
         default:
            return FALSE;
      }
      return TRUE;
   }
   LPDIRECT3DDEVICE9 WINAPI GetD3D9Device() {
   #pragma ExportedFunction
      if (!isExtenderReady)
         return nullptr;

      return d3dDevice;
   }
   HWND WINAPI GetWindowHandle() {
   #pragma ExportedFunction
      if (!isExtenderReady)
         return nullptr;

      D3DDEVICE_CREATION_PARAMETERS cParams;
      d3dDevice->GetCreationParameters(&cParams);
      return cParams.hFocusWindow;
   }
   bool WINAPI IsReady() {
   #pragma ExportedFunction
      return isExtenderReady;
   }
#pragma endregion

   void Init() {
      DWORD d3dDeviceAddress = NULL;
      while (!d3dDeviceAddress) {
         d3dDeviceAddress = *(DWORD*)Memory::makeAbsolute(0x582BDC);
         Sleep(100);
      }
      d3dDevice = (LPDIRECT3DDEVICE9)d3dDeviceAddress;

      d3dDeviceHook       = make_unique<VTableHook>((PDWORD*)d3dDevice);
      origReset           = d3dDeviceHook->Hook(16, hkReset);
      origBeginScene      = d3dDeviceHook->Hook(41, hkBeginScene);
      origEndScene        = d3dDeviceHook->Hook(42, hkEndScene);
      origBeginStateBlock = d3dDeviceHook->Hook(60, hkBeginStateBlock);

      isExtenderReady = true;
   }
}

namespace DirectInput8Extenders {
   LPDIRECTINPUT8A          di8Instance                       = nullptr;
   LPDIRECTINPUTDEVICE8A    device_Keyboard                   = nullptr;
   LPDIRECTINPUTDEVICE8A    device_Mouse                      = nullptr;

   map<DirectInput8Device, vector<GetDeviceState_t>> mGetDeviceStateExtenders;

   bool                     isExtenderReady                   = false;

#pragma region function hooks
   GetDeviceState_t         origGetDeviceState_Keyboard       = nullptr;
   GetDeviceState_t         origGetDeviceState_Mouse          = nullptr;

   HRESULT WINAPI hkGetDeviceState_Keyboard(HINSTANCE hInstance, DWORD cbData, LPVOID lpvData) {
      HRESULT retOrigGetDeviceState = origGetDeviceState_Keyboard(hInstance, cbData, lpvData);

      if (!mGetDeviceStateExtenders[DirectInput8Device::Keyboard].empty()) {
         for (GetDeviceState_t keyboardExtender : mGetDeviceStateExtenders[DirectInput8Device::Keyboard]) {
            if (keyboardExtender)
               keyboardExtender(hInstance, cbData, lpvData);
         }
      }
      return retOrigGetDeviceState;
   }
   HRESULT WINAPI hkGetDeviceState_Mouse(HINSTANCE hInstance, DWORD cbData, LPVOID lpvData) {
      HRESULT retOrigGetDeviceState = origGetDeviceState_Mouse(hInstance, cbData, lpvData);

      if (!mGetDeviceStateExtenders[DirectInput8Device::Mouse].empty()) {
         for (GetDeviceState_t mouseExtender : mGetDeviceStateExtenders[DirectInput8Device::Mouse]) {
            if (mouseExtender)
               mouseExtender(hInstance, cbData, lpvData);
         }
      }
      return retOrigGetDeviceState;
   }
#pragma endregion

#pragma region exported helpers
   HRESULT WINAPI AddDirectInput8Extender(DirectInput8Device deviceType, DirectInput8Extender extenderType, LPVOID extenderAddress) {
   #pragma ExportedFunction
      if (!isExtenderReady)
         return FALSE;

      switch (extenderType) {
         case DirectInput8Extender::GetDeviceState:
         {
            switch (deviceType) {
               case DirectInput8Device::Keyboard:
               case DirectInput8Device::Mouse:
                  mGetDeviceStateExtenders[deviceType].push_back(reinterpret_cast<GetDeviceState_t>(extenderAddress));
                  break;
               default:
                  return FALSE;
            }
            break;
         }
         default:
            return FALSE;
      }
      return TRUE;
   }
   LPDIRECTINPUT8A WINAPI GetDirectInput8AInstance() {
   #pragma ExportedFunction
      if (!isExtenderReady)
         return nullptr;

      return di8Instance;
   }
   LPDIRECTINPUTDEVICE8A WINAPI GetDirectInputDevice8A(DirectInput8Device deviceType) {
   #pragma ExportedFunction
      if (!isExtenderReady)
         return nullptr;

      switch (deviceType) {
         case DirectInput8Device::Keyboard:
            return device_Keyboard;
         case DirectInput8Device::Mouse:
            return device_Mouse;
         default:
            return nullptr;
      }
   }
   bool WINAPI IsReady() {
   #pragma ExportedFunction
      return isExtenderReady;
   }
#pragma endregion

   BOOL CALLBACK enumCallback(LPCDIDEVICEINSTANCEA lpddi, LPVOID) {
      BYTE deviceType = LOBYTE(lpddi->dwDevType);
      if (deviceType == DI8DEVTYPE_KEYBOARD || deviceType == DI8DEVTYPE_MOUSE) {
         DWORD* inputTable;

         if (deviceType == DI8DEVTYPE_KEYBOARD) {
            di8Instance->CreateDevice(lpddi->guidInstance, &device_Keyboard, NULL);
            inputTable = *(PDWORD*)device_Keyboard;
         }
         else {
            di8Instance->CreateDevice(lpddi->guidInstance, &device_Mouse, NULL);
            inputTable = *(PDWORD*)device_Mouse;
         }

         Memory::openMemoryAccess(inputTable[9], 4);

         if (deviceType == DI8DEVTYPE_KEYBOARD) {
            origGetDeviceState_Keyboard = (GetDeviceState_t)(DWORD)inputTable[9];
            inputTable[9]               = (DWORD)hkGetDeviceState_Keyboard;
         }
         else {
            origGetDeviceState_Mouse    = (GetDeviceState_t)(DWORD)inputTable[9];
            inputTable[9]               = (DWORD)hkGetDeviceState_Mouse;
         }

         Memory::restoreMemoryAccess();
      }

      if (device_Keyboard && device_Mouse) {
         isExtenderReady = true;
         return DIENUM_STOP;
      }
      return DIENUM_CONTINUE;
   }
   void Init() {
      mGetDeviceStateExtenders[DirectInput8Device::Keyboard] = vector<GetDeviceState_t>();
      mGetDeviceStateExtenders[DirectInput8Device::Mouse]    = vector<GetDeviceState_t>();

      DWORD dinput8Address = NULL;
      while (!dinput8Address) {
         dinput8Address = *(DWORD*)Memory::makeAbsolute(0x582D14);
         Sleep(100);
      }
      di8Instance = (LPDIRECTINPUT8A)dinput8Address;
      di8Instance->EnumDevices(DI8DEVCLASS_ALL, &enumCallback, NULL, DIEDFL_ATTACHEDONLY);
   }
}

bool isInit = false;

bool WINAPI IsReady() {
#pragma ExportedFunction
   return isInit;
}

DWORD WINAPI Init(LPVOID) {
   Memory::Init();
   D3D9Extenders::Init();
   DirectInput8Extenders::Init();

   isInit = true;
   return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
   if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
      DisableThreadLibraryCalls(hModule);
      CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&Init, 0, 0, 0);
   }
   return TRUE;
}