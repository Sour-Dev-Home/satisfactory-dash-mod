#include "SatisModS4Config.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"

USatisModS4Config::USatisModS4Config()
{
	ConfigId.ModReference = TEXT("SatisModS4");
	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));
	UConfigPropertyString* Credential = CreateDefaultSubobject<UConfigPropertyString>(TEXT("credential"));
	RootSection->SectionProperties.Add(TEXT("credential"), Credential);
}
