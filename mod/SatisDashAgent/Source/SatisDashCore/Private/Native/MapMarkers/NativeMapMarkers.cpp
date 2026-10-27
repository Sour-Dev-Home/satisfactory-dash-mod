#include "SatisDash/Native/MapMarkers/NativeMapMarkers.h"

#include "SatisDash/FrmMap.h"
#include "SatisDash/FrmRecords.h"
#include "SatisDash/JsonUtil.h"
#include "SatisDash/Snapshot.h"

namespace
{
	/** A marker after the checks: its id and its item text, which together are the sort key. */
	struct FNativeMarkerItem
	{
		FString Id;
		FString Json;
	};

	bool NativeMarkerItemLess(const FNativeMarkerItem& A, const FNativeMarkerItem& B)
	{
		const int32 ById = A.Id.Compare(B.Id, ESearchCase::CaseSensitive);
		if (ById != 0)
		{
			return ById < 0;
		}
		return A.Json.Compare(B.Json, ESearchCase::CaseSensitive) < 0;
	}

	/** JavaScript String.prototype.trim's set: WhiteSpace and LineTerminator. */
	bool NativeMarkerIsJsSpace(TCHAR C)
	{
		switch (uint32(C))
		{
		case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0: case 0x1680:
		case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
			return true;
		default:
			return uint32(C) >= 0x2000 && uint32(C) <= 0x200A;
		}
	}
}

FString SatisDash::MapMarkerName(const FString& Raw)
{
	FString Fixed;
	Fixed.Reserve(Raw.Len());
	const int32 Len = Raw.Len();
	for (int32 I = 0; I < Len; ++I)
	{
		const uint32 C = uint32(Raw[I]);
		if (C >= 0xD800 && C <= 0xDBFF && I + 1 < Len && uint32(Raw[I + 1]) >= 0xDC00 && uint32(Raw[I + 1]) <= 0xDFFF)
		{
			Fixed.AppendChar(Raw[I]);
			Fixed.AppendChar(Raw[++I]);
		}
		else if (C >= 0xD800 && C <= 0xDFFF)
		{
			Fixed.AppendChar(TCHAR(0xFFFD)); // a surrogate half with no partner
		}
		else
		{
			Fixed.AppendChar(Raw[I]);
		}
	}
	int32 Begin = 0;
	int32 End = Fixed.Len();
	while (Begin < End && NativeMarkerIsJsSpace(Fixed[Begin]))
	{
		++Begin;
	}
	while (End > Begin && NativeMarkerIsJsSpace(Fixed[End - 1]))
	{
		--End;
	}
	return ClampStringTo(Fixed.Mid(Begin, End - Begin), MapMarkerNameMaxChars);
}

FString SatisDash::MapMarkerColorHex(const FLinearColor& Color, bool bSRGB)
{
	if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B))
	{
		return TEXT("#ffffff");
	}
	const FString Hex = Color.ToFColor(bSRGB).ToHex(); // RRGGBBAA, upper case
	return TEXT("#") + Hex.Left(6).ToLower();
}

SatisDash::FNativeWorldBody SatisDash::BuildNativeMapMarkers(const FString& ObservedAt, const TArray<FMapMarkerReading>& Readings)
{
	FNativeWorldBody Out;

	TArray<FNativeMarkerItem> Items;
	Items.Reserve(Readings.Num());
	for (const FMapMarkerReading& Reading : Readings)
	{
		FNativeMarkerItem Item;
		Item.Id = ClampString(Reading.Id);
		const double Mx = ToWholeMetres(Reading.XCm);
		const double My = ToWholeMetres(Reading.YCm);
		if (Item.Id.IsEmpty() || !InWholeMetreBounds(Mx) || !InWholeMetreBounds(My) || Reading.Icon < 0 || Reading.Icon > MapMarkerMaxIcon)
		{
			++Out.Dropped;
			continue;
		}
		Item.Json = TEXT("{\"id\":");
		SatisDash::Json::AppendString(Item.Json, Item.Id);
		Item.Json += TEXT(",\"name\":");
		SatisDash::Json::AppendString(Item.Json, MapMarkerName(Reading.Name));
		Item.Json += Reading.Kind == EMapMarkerKind::Stamp ? TEXT(",\"kind\":\"stamp\"") : TEXT(",\"kind\":\"marker\"");
		Item.Json += TEXT(",\"icon\":");
		Item.Json.AppendInt(Reading.Icon);
		Item.Json += TEXT(",\"color\":");
		SatisDash::Json::AppendString(Item.Json, MapMarkerColorHex(Reading.Color, NativeMarkerColorSRGB));
		Item.Json += TEXT(",\"x\":");
		Item.Json.AppendInt(int32(Mx));
		Item.Json += TEXT(",\"y\":");
		Item.Json.AppendInt(int32(My));
		Item.Json += TEXT("}");
		Items.Add(MoveTemp(Item));
	}
	Items.Sort(NativeMarkerItemLess);

	Out.Json = WorldBodyPrefix(ObservedAt);
	Out.DataFrom = Out.Json.Len();
	Out.Json.AppendChar(TEXT('['));
	const FNativeMarkerItem* Previous = nullptr;
	for (const FNativeMarkerItem& Item : Items)
	{
		if (Previous && Previous->Id.Equals(Item.Id, ESearchCase::CaseSensitive))
		{
			continue; // a repeated id: the one that sorted first stays
		}
		Previous = &Item;
		if (Out.Items == MapMarkersMaxItems)
		{
			++Out.Over;
			continue;
		}
		if (Out.Items > 0)
		{
			Out.Json.AppendChar(TEXT(','));
		}
		Out.Json += Item.Json;
		++Out.Items;
	}
	Out.Json.AppendChar(TEXT(']'));
	Out.DataEnd = Out.Json.Len();
	Out.Json += TEXT("}");
	Out.Bytes = FTCHARToUTF8_Convert::ConvertedLength(*Out.Json, Out.Json.Len());
	return Out;
}
