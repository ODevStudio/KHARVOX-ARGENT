#pragma once
#include "ControllerInput.h"
#include "XInputHapticsPolicy.h"
namespace argent::input {
using SetState=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
inline std::atomic<SetState> originalSetState{};
inline std::atomic<uint32_t> rumble{},rumblePeak{};
inline std::atomic<ULONGLONG> rumbleTick{};
inline DWORD WINAPI setState(DWORD user,XINPUT_VIBRATION* vibration){
 const auto native=originalSetState.load();const auto result=native?native(user,vibration):ERROR_DEVICE_NOT_CONNECTED;
 if(user||!vibration)return result;
 const auto value=kharvox::packXInputRumble(vibration->wLeftMotorSpeed,vibration->wRightMotorSpeed);
 rumble=value;rumbleTick=GetTickCount64();auto old=rumblePeak.load();while(!rumblePeak.compare_exchange_weak(old,kharvox::mergeXInputRumblePeaks(old,value))){}
 return fresh(snapshot(),GetTickCount64())?ERROR_SUCCESS:result;
}
inline bool installHaptics() noexcept {return installXInputIatHook("XInputSetState",&setState,originalSetState);}
}
