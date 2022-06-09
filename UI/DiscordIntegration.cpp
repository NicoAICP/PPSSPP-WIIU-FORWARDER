
#include <ctime>

#include "ppsspp_config.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "DiscordIntegration.h"
#include "Common/Data/Text/I18n.h"

#if (PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(LINUX)) && !PPSSPP_PLATFORM(ANDROID) && !PPSSPP_PLATFORM(UWP)

#ifdef _MSC_VER
#define ENABLE_DISCORD
#elif USE_DISCORD
#define ENABLE_DISCORD
#endif

#else

// TODO

#endif

#ifdef ENABLE_DISCORD
#include "ext/discord-rpc/include/discord_rpc.h"
#endif

// TODO: Enable on more platforms. Make optional.

Discord g_Discord;

static const char *ppsspp_app_id = "423397985041383434";

#ifdef ENABLE_DISCORD
// No context argument? What?
static void handleDiscordError(int errCode, const char *message) {
	ERROR_LOG(SYSTEM, "Discord error code %d: '%s'", errCode, message);
}
#endif

Discord::~Discord() {
	if (initialized_) {
		ERROR_LOG(SYSTEM, "Discord destructor running though g_Discord.Shutdown() has not been called.");
	}
}

bool Discord::IsEnabled() const {
	return g_Config.bDiscordPresence;
}

void Discord::Init() {
	_assert_(IsEnabled());
	_assert_(!initialized_);

#ifdef ENABLE_DISCORD
	DiscordEventHandlers eventHandlers{};
	eventHandlers.errored = &handleDiscordError;
	Discord_Initialize(ppsspp_app_id, &eventHandlers, 0, nullptr);
	INFO_LOG(SYSTEM, "Discord connection initialized");
#endif

	initialized_ = true;
}

void Discord::Shutdown() {
	if (initialized_) {
#ifdef ENABLE_DISCORD
		Discord_Shutdown();
#endif
		initialized_ = false;
	}
}

void Discord::Update() {
	if (!IsEnabled()) {
		if (initialized_) {
			Shutdown();
		}
		return;
	} else {
		if (!initialized_) {
			Init();
		}
	}

#ifdef ENABLE_DISCORD
#ifdef DISCORD_DISABLE_IO_THREAD
	Discord_UpdateConnection();
#endif
	Discord_RunCallbacks();
#endif
}

void Discord::SetPresenceGame(const char *gameTitle) {
	if (!IsEnabled())
		return;
	
	if (!initialized_) {
		Init();
	}

#ifdef ENABLE_DISCORD
	auto sc = GetI18NCategory("Screen");

	DiscordRichPresence discordPresence{};
	discordPresence.state = gameTitle;
	std::string details = sc->T("Playing");
	discordPresence.details = details.c_str();
	discordPresence.startTimestamp = time(0);
	discordPresence.largeImageText = "PPSSPP is the best PlayStation Portable emulator around!";
#ifdef GOLD
	discordPresence.largeImageKey = "icon_gold_png";
#else
	discordPresence.largeImageKey = "icon_regular_png";
#endif
	Discord_UpdatePresence(&discordPresence);
#endif
}

void Discord::SetPresenceMenu() {
	if (!IsEnabled())
		return;

	if (!initialized_) {
		Init();
	}

#ifdef ENABLE_DISCORD
	auto sc = GetI18NCategory("Screen");

	DiscordRichPresence discordPresence{};
	discordPresence.state = sc->T("In menu");
	discordPresence.details = "";
	discordPresence.startTimestamp = time(0);
	discordPresence.largeImageText = "PPSSPP is the best PlayStation Portable emulator around!";
#ifdef GOLD
	discordPresence.largeImageKey = "icon_gold_png";
#else
	discordPresence.largeImageKey = "icon_regular_png";
#endif
	Discord_UpdatePresence(&discordPresence);
#endif
}

void Discord::ClearPresence() {
	if (!IsEnabled() || !initialized_)
		return;

#ifdef ENABLE_DISCORD
	Discord_ClearPresence();
#endif
}
