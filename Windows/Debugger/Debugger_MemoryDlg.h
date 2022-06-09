// NOTE: Apologies for the quality of this code, this is really from pre-opensource Dolphin - that is, 2003.

#pragma once
#include "Windows/W32Util/DialogManager.h"

#include "Core/MemMap.h"

#include "Core/Debugger/DebugInterface.h"
#include "CtrlMemView.h"
#include "Common/CommonWindows.h"

class CMemoryDlg : public Dialog
{
private:
	DebugInterface *cpu;
	static RECT slRect;
	RECT winRect, srRect;
	CtrlMemView *memView;
	HWND memViewHdl, symListHdl, editWnd, searchBoxHdl, srcListHdl;
	BOOL DlgProc(UINT message, WPARAM wParam, LPARAM lParam);

public:
	int index; //helper 

	void searchBoxRedraw(std::vector<u32> results);

	// constructor
	CMemoryDlg(HINSTANCE _hInstance, HWND _hParent, DebugInterface *_cpu);
	
	// destructor
	~CMemoryDlg(void);
	
	void Goto(u32 addr);
	void Update(void);	
	void NotifyMapLoaded();

	void NotifySearchCompleted();

	void Size(void);
};


