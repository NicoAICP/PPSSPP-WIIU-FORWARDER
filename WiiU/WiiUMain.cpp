#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <filesystem>

#include <wiiu/os/systeminfo.h>
#include <wiiu/os/thread.h>
#include <wiiu/os/debug.h>
#include <wiiu/gx2/event.h>
#include <wiiu/ax/core.h>
#include <iosuhax.h>
#include <iosuhax_devoptab.h>
#include <wiiu/ios.h>

#include <sys/stat.h>

#include "Common/Profiler/Profiler.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Core/Core.h"
#include "Common/Log.h"

#include "Common/GraphicsContext.h"
#include "WiiU/WiiUHost.h"


const char *PROGRAM_NAME = "PPSSPP";
const char *PROGRAM_VERSION = "NICO WAS HERE";

static int g_QuitRequested;
void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		g_QuitRequested = true;
		UpdateUIState(UISTATE_EXIT);
		Core_Stop();
	}
}

bool file_exists (const std::string &s)
{
  struct stat buffer;
  return (stat (s.c_str(), &buffer) == 0);
}
bool IsPathExist(const std::string &s)
{
  struct stat buffer;
  return (stat (s.c_str(), &buffer) == 0);
}

void bytes2hex(uint64_t input, char* output) {
    const char table[] = "0123456789ABCDEF";
    for(size_t i = 0, o = 15; i != 16; i++, o--) {
        output[o] = table[(input >> (i * 4)) & 0xF];
    }
}




int main(int argc, char **argv) {
	IOSUHAX_Open(NULL);
	int fsaHandle = IOSUHAX_FSA_Open();
	mount_fs("storage_Nand", fsaHandle, NULL, "/vol/storage_mlc01");
	mount_fs("storage_usb", fsaHandle, NULL, "/vol/storage_usb01");
	PROFILE_INIT();
	PPCSetFpIEEEMode();

	host = new WiiUHost();

	std::string app_name;
	std::string app_name_nice;
	std::string version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape, &version);

	std::ofstream fw("sd:/NICOLOG.txt", std::ofstream::out);
	uint64_t tID;
   	tID = OSGetTitleID();
   	char titleIDHex[16];
   	bytes2hex(tID, titleIDHex);
	   if (fw.is_open())
		{
			 fw << titleIDHex << "\n";
		}
   	char usbPath[] = "storage_usb:/usr/title/00050002/XXXXXXXX/content/game.iso";
   	memcpy(&usbPath[23], titleIDHex, 8);
	    if (fw.is_open())
		{
			 fw << usbPath << "\n";
		}
   	memcpy(&usbPath[32], titleIDHex + 8, 8);
	    if (fw.is_open())
		{
			 fw << usbPath << "\n";
		}
   	if (!file_exists(usbPath)) {
      memcpy(&usbPath[7], "Nand", 4);
	   if (fw.is_open())
		{
			 fw << usbPath << "\n";
		}
   	}
	if (fw.is_open())
		{
			 fw << "Trying to copy file" << "\n";
		}
	
	if(file_exists("sd:/ppsspp/game.iso")){
		if (fw.is_open())
		{
			 fw << "Deleting game.iso"<< "\n";
		}
		std::remove("sd:/ppsspp/game.iso");
	}

	FILE * filer, * filew;
	int numr,numw;
	char buffer[] = memalign(0x40, 0x20000);

	if((filer=fopen(usbPath,"rb"))==NULL){
			if (fw.is_open())
		{
			 fw << "Cant read usb"<< "\n";
		}
		exit(1);
	}

	if((filew=fopen("sd:/ppsspp/game.iso","wb"))==NULL){
			if (fw.is_open())
		{
			 fw << "write file error thingy good logs i know"<< "\n";
		}
		exit(1);
	}

	while(feof(filer)==0){	
	if((numr=fread(buffer,1,1024,filer))!=1024){
		if(ferror(filer)!=0){
				if (fw.is_open())
		{
			 fw << "read error"<< "\n";
		}
		exit(1);
		}
		else if(feof(filer)!=0);
	}
	if((numw=fwrite(buffer,1,numr,filew))!=numr){
			if (fw.is_open())
		{
			 fw << "write error"<< "\n";
		}
		exit(1);
	}
	}	
	if (fw.is_open())
		{
			 fw << "wrote file\nTrying to close usb file"<< "\n";
		}
	fclose(filer);
	if (fw.is_open())
		{
			 fw << "closed usb file/ trying to close sd file"<< "\n";
		}
	fclose(filew);
if (fw.is_open())
		{
			 fw << "loading game i guess"<< "\n";
		}
	
	const char *argv_[] = {
		"sd:/ppsspp/PPSSPP.rpx",
		"sd:/ppsspp/game.iso",
//		"-d",
//		"-v",
//		"-j",
//		"-r",
//		"-i",
//		"sd:/cube.elf",
		nullptr
	};

	//arg size, arg, savegamedir, external dir, cache dir
	NativeInit(sizeof(argv_) / sizeof(*argv_) - 1, argv_, "sd:/ppsspp/", "sd:/ppsspp/", nullptr);
	//NativeInit(sizeof(argv_) / sizeof(*argv_) - 1, argv_, usbPath, usbPath, nullptr);
#if 0
	UpdateScreenScale(854,480);
#else
	float dpi_scale = 1.0f;
	g_dpi = 96.0f;
	pixel_xres = 854;
	pixel_yres = 480;
	dp_xres = (float)pixel_xres * dpi_scale;
	dp_yres = (float)pixel_yres * dpi_scale;
	pixel_in_dps_x = (float)pixel_xres / dp_xres;
	pixel_in_dps_y = (float)pixel_yres / dp_yres;
	g_dpi_scale_x = dp_xres / (float)pixel_xres;
	g_dpi_scale_y = dp_yres / (float)pixel_yres;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;
#endif
	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);

	g_Config.iPSPModel = PSP_MODEL_SLIM;
	g_Config.iGPUBackend = (int)GPUBackend::GX2;
	g_Config.bEnableSound = true;
	g_Config.bPauseExitsEmulator = false;
	g_Config.bPauseMenuExitsEmulator = false;
//	g_Config.iCpuCore = (int)CPUCore::JIT;
	g_Config.bVertexDecoderJit = false;
	g_Config.bSoftwareRendering = false;
//	g_Config.iFpsLimit = 0;
	g_Config.bHardwareTransform = true;
	g_Config.bSoftwareSkinning = false;
	g_Config.bVertexCache = true;
//	PSP_CoreParameter().gpuCore = GPUCORE_NULL;
//	g_Config.bTextureBackoffCache = true;
	std::string error_string;
	GraphicsContext *ctx;
	host->InitGraphics(&error_string, &ctx);
	NativeInitGraphics(ctx);
	NativeResized();

	host->InitSound();
	while (true) {
		if (g_QuitRequested)
			break;

		if (!Core_IsActive())
			UpdateUIState(UISTATE_MENU);
		Core_Run(ctx);
	}
	host->ShutdownSound();
	//unmount_fs("storage_Nand");
	//unmount_fs("storage_usb");
	NativeShutdownGraphics();
	NativeShutdown();

	return 0;
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
		return "Wii-U";
	case SYSPROP_LANGREGION:
		return "en_US";
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000; // internal refresh rate is always 59.940, even for PAL output.
	case SYSPROP_DISPLAY_XRES:
		return 854;
	case SYSPROP_DISPLAY_YRES:
		return 480;
	case SYSPROP_DEVICE_TYPE:
		return DEVICE_TYPE_TV;
	case SYSPROP_AUDIO_SAMPLE_RATE:
	case SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE:
		return 48000;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
	case SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER:
		return AX_FRAME_SIZE;
	case SYSPROP_SYSTEMVERSION:
	default:
		return -1;
	}
}
bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	default:
		return false;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60.0f;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return 0.0f;
	default:
		return -1;
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void LaunchBrowser(const char *url) {}
void ShowKeyboard() {}
void Vibrate(int length_ms) {}
