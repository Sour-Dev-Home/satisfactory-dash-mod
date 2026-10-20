#include "SatisModS5Config.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"

USatisModS5Config::USatisModS5Config()
{
	ConfigId.ModReference = TEXT("SatisModS5");
	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));
	UConfigPropertyString* Code = CreateDefaultSubobject<UConfigPropertyString>(TEXT("code"));
	RootSection->SectionProperties.Add(TEXT("code"), Code);
	UConfigPropertyString* BackendUrl = CreateDefaultSubobject<UConfigPropertyString>(TEXT("backendUrl"));
	RootSection->SectionProperties.Add(TEXT("backendUrl"), BackendUrl);
}
