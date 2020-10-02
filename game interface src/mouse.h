#pragma once

#include <Windows.h>
#include <iostream>
#include "globals.h"

short mouse_wheel_delta = 0;

long mouse_x = 0;
long mouse_y = 0;

// TO DO: simplify the mouse hook

// retrieved from https://www.unknowncheats.me/wiki/C%2B%2B:WindowsHookEx_Mouse
class MyHook {
public:
	//single ton
	static MyHook& Instance() {
		static MyHook myHook;
		return myHook;
	}
	HHOOK hook; // handle to the hook	
	void InstallHook(); // function to install our hook
	void UninstallHook(); // function to uninstall our hook

	MSG msg; // struct with information about all messages in our queue
	int Messsages(); // function to "deal" with our messages 
};
LRESULT WINAPI MyMouseCallback(int nCode, WPARAM wParam, LPARAM lParam); //callback declaration


int MyHook::Messsages() {
	while (msg.message != WM_QUIT && !quit) { //while we do not close our application
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		Sleep(1);
	}
	UninstallHook(); //if we close, let's uninstall our hook
	return (int)msg.wParam; //return the messages
}

void MyHook::InstallHook() {
	/*
	SetWindowHookEx(
	WM_MOUSE_LL = mouse low level hook type,
	MyMouseCallback = our callback function that will deal with system messages about mouse
	NULL, 0);

	c++ note: we can check the return SetWindowsHookEx like this because:
	If it return NULL, a NULL value is 0 and 0 is false.
	*/
	if (!(hook = SetWindowsHookEx(WH_MOUSE_LL, MyMouseCallback, NULL, 0))) {
		std::cout << "Error: " <<  GetLastError() << std::endl;
	}
}

void MyHook::UninstallHook() {
	/*
	uninstall our hook using the hook handle
	*/
	UnhookWindowsHookEx(hook);
}

LRESULT WINAPI MyMouseCallback(int nCode, WPARAM wParam, LPARAM lParam) {
	MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam; // WH_MOUSE_LL struct
	/*
	nCode, this parameters will determine how to process a message
	This callback in this case only have information when it is 0 (HC_ACTION): wParam and lParam contain info

	wParam is about WINDOWS MESSAGE, in this case MOUSE messages.
	lParam is information contained in the structure MSLLHOOKSTRUCT
	*/

	if (nCode == 0) { // we have information in wParam/lParam ? If yes, let's check it:

		if (pMouseStruct != NULL) { // Mouse struct contain information?			
			mouse_x = pMouseStruct->pt.x;
			mouse_y = pMouseStruct->pt.y;
		}

		switch (wParam) {

		case WM_MOUSEWHEEL:
		{
			mouse_wheel_delta += static_cast<signed short>HIWORD(pMouseStruct->mouseData);
			break;
		}
		}

	}

	/*
	Every time that the nCode is less than 0 we need to CallNextHookEx:
	-> Pass to the next hook
		 MSDN: Calling CallNextHookEx is optional, but it is highly recommended;
		 otherwise, other applications that have installed hooks will not receive hook notifications and may behave incorrectly as a result.
	*/
	return CallNextHookEx(MyHook::Instance().hook, nCode, wParam, lParam);
}

void UpdateMouseState()
{
	MyHook::Instance().InstallHook();
	MyHook::Instance().Messsages();
}

int GetAndClearWheelDelta()
{
	int d = mouse_wheel_delta;
	mouse_wheel_delta = 0;
	return d;
}

int* GetAndClearMouseDelta()
{
	static int prev_mouse_x = 0;
	static int prev_mouse_y = 0;

	int dx_dy[2];

	dx_dy[0] = mouse_x - prev_mouse_x;
	dx_dy[1] = mouse_y - prev_mouse_y;

	prev_mouse_x = mouse_x;
	prev_mouse_y = mouse_y;

	return dx_dy;
}

// set mouse click state and dont update until it receives a different click state
void SetMouseClickState(bool LMB, bool RMB)
{
	static bool last_lmb_state = false;
	static bool last_rmb_state = false;

	INPUT ip;
	ZeroMemory(&ip, sizeof(ip));

	ip.type = INPUT_MOUSE;

	if (LMB != last_lmb_state)
	{
		if (LMB)
			ip.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		else
			ip.mi.dwFlags = MOUSEEVENTF_LEFTUP;

		last_lmb_state = !last_lmb_state;
		SendInput(1, &ip, sizeof(INPUT));
	}

	if (RMB != last_rmb_state)
	{
		if (RMB)
			ip.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
		else
			ip.mi.dwFlags = MOUSEEVENTF_RIGHTUP;

		last_rmb_state = !last_rmb_state;
		SendInput(1, &ip, sizeof(INPUT));
	}
}

// send LMB down and up
void LeftClick()
{
	INPUT ip;
	ZeroMemory(&ip, sizeof(ip));

	ip.type = INPUT_MOUSE;

	ip.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
	SendInput(1, &ip, sizeof(INPUT));
	ip.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	SendInput(1, &ip, sizeof(INPUT));
	ip.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	SendInput(1, &ip, sizeof(INPUT));
}

UINT ScrollMouse(int scroll)
{
	INPUT input;
	ZeroMemory(&input, sizeof(input));

	POINT pos;
	GetCursorPos(&pos);

	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_WHEEL;
	input.mi.time = NULL;
	input.mi.mouseData = (DWORD)scroll;
	input.mi.dx = pos.x;
	input.mi.dy = pos.y;
	input.mi.dwExtraInfo = GetMessageExtraInfo();

	return SendInput(1, &input, sizeof(INPUT));
}

void MoveMouse(long x, long y)
{
	INPUT  Input = { 0 };
	Input.type = INPUT_MOUSE;
	Input.mi.dwFlags = MOUSEEVENTF_MOVE;
	Input.mi.dx = x;
	Input.mi.dy = y;
	SendInput(1, &Input, sizeof(INPUT));
}
