#include "SatisDash/FrmMapLive.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

SatisDash::FTrainsParse::FTrainsParse(TArray<uint8> InBody)
	: FFrmArrayParse(MoveTemp(InBody), MapLiveMaxItems, FString(), FString())
{
}

auto SatisDash::FTrainsParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Id;
	TOptional<FString> Name;
	TOptional<FString> Status;
	bool bLocation = false;
	double X = 0;
	double Y = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Id.IsSet() || Id->IsEmpty() || !Name.IsSet() || !Status.IsSet() || !bLocation)
			{
				return ERecord::Invalid;
			}
			const double Mx = ToWholeMetres(X);
			const double My = ToWholeMetres(Y);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"id\":");
			SatisDash::Json::AppendString(Out, ClampString(Id.GetValue()));
			Out += TEXT(",\"name\":");
			SatisDash::Json::AppendString(Out, ClampString(Name.GetValue()));
			Out += TEXT(",\"x\":");
			Out.AppendInt(int32(Mx));
			Out += TEXT(",\"y\":");
			Out.AppendInt(int32(My));
			Out += TEXT(",\"status\":");
			SatisDash::Json::AppendString(Out, ClampString(Status.GetValue()));
			Out.AppendChar(TEXT('}'));
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		const bool bId = Is(Key, TEXT("ID"));
		const bool bName = Is(Key, TEXT("Name"));
		const bool bStatus = Is(Key, TEXT("Status"));
		if (bId || bName || bStatus)
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			(bId ? Id : bName ? Name : Status) = Reader.String();
		}
		else if (Is(Key, TEXT("location")))
		{
			if (!ReadFrmLocation(Reader, N, X, Y))
			{
				return ERecord::Invalid;
			}
			bLocation = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}

SatisDash::FStationsParse::FStationsParse(TArray<uint8> InBody)
	: FFrmArrayParse(MoveTemp(InBody), MapLiveMaxItems, FString(), FString())
{
}

auto SatisDash::FStationsParse::ReadRecord(bool bRoom) -> ERecord
{
	TOptional<FString> Id;
	TOptional<FString> Name;
	bool bLocation = false;
	double X = 0;
	double Y = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (!Id.IsSet() || Id->IsEmpty() || !Name.IsSet() || !bLocation)
			{
				return ERecord::Invalid;
			}
			const double Mx = ToWholeMetres(X);
			const double My = ToWholeMetres(Y);
			if (!InWholeMetreBounds(Mx) || !InWholeMetreBounds(My))
			{
				return ERecord::Dropped;
			}
			if (!bRoom)
			{
				return ERecord::Over;
			}
			BeginRecord();
			Out += TEXT("{\"id\":");
			SatisDash::Json::AppendString(Out, ClampString(Id.GetValue()));
			Out += TEXT(",\"name\":");
			SatisDash::Json::AppendString(Out, ClampString(Name.GetValue()));
			Out += TEXT(",\"x\":");
			Out.AppendInt(int32(Mx));
			Out += TEXT(",\"y\":");
			Out.AppendInt(int32(My));
			Out.AppendChar(TEXT('}'));
			return ERecord::Kept;
		}
		const FString Key = Reader.Name();
		const bool bId = Is(Key, TEXT("ID"));
		if (bId || Is(Key, TEXT("Name")))
		{
			if (N != EJsonNotation::String)
			{
				return ERecord::Invalid;
			}
			(bId ? Id : Name) = Reader.String();
		}
		else if (Is(Key, TEXT("location")))
		{
			if (!ReadFrmLocation(Reader, N, X, Y))
			{
				return ERecord::Invalid;
			}
			bLocation = true;
		}
		else if (!Reader.SkipValue(N))
		{
			return ERecord::Invalid;
		}
	}
	return ERecord::Invalid;
}

TOptional<FString> SatisDash::JoinMapLive(const FString& TrainsArray, int64 TrainsBytes, const FString& StationsArray, int64 StationsBytes)
{
	if (MapLiveWrapperBytes + TrainsBytes + StationsBytes > MapLiveMaxBytes)
	{
		return {};
	}
	FString Out;
	Out.Reserve(TrainsArray.Len() + StationsArray.Len() + 24);
	Out += TEXT("{\"trains\":");
	Out += TrainsArray;
	Out += TEXT(",\"stations\":");
	Out += StationsArray;
	Out.AppendChar(TEXT('}'));
	return Out;
}
