// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once

#include "Windows/W32Util/DialogManager.h"
#include "Windows/W32Util/TabControl.h"
#include "Windows/Debugger/CtrlDisasmView.h"
#include "Windows/Debugger/Debugger_Lists.h"
#include "Windows/Debugger/CPURegsInterface.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/Debugger/Breakpoints.h"
#include <vector>

#include "Common/CommonWindows.h"

class CDisasm : public Dialog
{
private:
	int minWidth;
	int minHeight;
	DebugInterface *cpu;
	u64 lastTicks;

	HWND statusBarWnd;
	CtrlBreakpointList* breakpointList;
	CtrlThreadList* threadList;
	CtrlStackTraceView* stackTraceView;
	CtrlModuleList* moduleList;
	TabControl* leftTabs;
	TabControl* bottomTabs;
	std::vector<BreakPoint> displayedBreakPoints_;
	std::vector<MemCheck> displayedMemChecks_;
	bool keepStatusBarText = false;
	bool hideBottomTabs = false;
	bool deferredSymbolFill_ = false;

	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam) override;
	void UpdateSize(WORD width, WORD height);
	void SavePosition();
	void updateThreadLabel(bool clear);
	void stepInto();
	void stepOver();
	void stepOut();
	void runToLine();

public:
	int index;

	CDisasm(HINSTANCE _hInstance, HWND _hParent, DebugInterface *cpu);
	~CDisasm();

	void Show(bool bShow) override;

	void Update() override {
		UpdateDialog(true);
		SetDebugMode(Core_IsStepping(), false);
		breakpointList->reloadBreakpoints();
	};
	void UpdateDialog(bool _bComplete = false);
	// SetDebugMode 
	void SetDebugMode(bool _bDebug, bool switchPC);

	void Goto(u32 addr);
	void NotifyMapLoaded();
};
