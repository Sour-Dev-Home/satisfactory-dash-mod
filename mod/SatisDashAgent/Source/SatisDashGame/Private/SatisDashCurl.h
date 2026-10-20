#pragma once

// The mod's own libcurl (ThirdParty libcurl 8.12.1), included as the engine does it (CurlHttp.h:16-28): Windows types are
// allowed around curl.h so its winsock declarations don't clash with UE's.
#include "HAL/Platform.h"

#if PLATFORM_MICROSOFT
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif

#include "curl/curl.h"

#if PLATFORM_MICROSOFT
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#endif
