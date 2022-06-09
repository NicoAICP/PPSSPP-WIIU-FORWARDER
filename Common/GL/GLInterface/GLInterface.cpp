// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "ppsspp_config.h"
#include "Common/GL/GLInterfaceBase.h"

#ifdef __ANDROID__
#include "Common/GL/GLInterface/EGLAndroid.h"
#elif PPSSPP_PLATFORM(SWITCH)
#include "Common/GL/GLInterface/EGLSwitch.h"
#elif defined(__APPLE__)
#include "Common/GL/GLInterface/AGL.h"
#elif defined(_WIN32)
#include "Common/GL/GLInterface/WGL.h"
#elif HAVE_X11
#if defined(USE_EGL) && USE_EGL
#include "Common/GL/GLInterface/EGLX11.h"
#else
#include "Common/GL/GLInterface/GLX.h"
#endif
#else
#error Platform doesnt have a GLInterface
#endif

cInterfaceBase* HostGL_CreateGLInterface(){
	#ifdef __ANDROID__
		return new cInterfaceEGLAndroid;
	#elif if PPSSPP_PLATFORM(SWITCH)
		return new cInterfaceEGLSwitch;
	#elif defined(__APPLE__)
		return new cInterfaceAGL;
	#elif defined(_WIN32)
		return new cInterfaceWGL;
	#elif defined(HAVE_X11) && HAVE_X11
	#if defined(USE_EGL) && USE_EGL
		return new cInterfaceEGLX11;
	#else
		return new cInterfaceGLX;
	#endif
	#else
		return nullptr;
	#endif
}
