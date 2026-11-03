#include "SatisDash/PlayersPart.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

SatisDash::FPlayersPart SatisDash::PlayersUnavailable()
{
	return FPlayersPart();
}

FString SatisDash::PlayersToJson(const FPlayersPart& Part)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("available"), Part.bAvailable);
	Writer->WriteArrayStart(TEXT("players"));
	for (const FPlayerReading& Player : Part.Players)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Player.Name);
		Writer->WriteValue(TEXT("online"), Player.bOnline);
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return Out;
}
