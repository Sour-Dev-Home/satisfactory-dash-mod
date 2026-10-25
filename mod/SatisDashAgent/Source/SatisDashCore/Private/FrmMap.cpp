#include "SatisDash/FrmMap.h"

namespace
{
	bool FrmMapIs(const FString& Key, const TCHAR* Name)
	{
		return Key.Equals(Name, ESearchCase::CaseSensitive);
	}
}

double SatisDash::JsRound(double Value)
{
	if (!FMath::IsFinite(Value))
	{
		return Value;
	}
	// value - floor(value) is exact, so the half test is exact too.
	const double Floor = FMath::FloorToDouble(Value);
	return Value - Floor >= 0.5 ? Floor + 1.0 : Floor;
}

double SatisDash::ToWholeMetres(double GameUnits)
{
	// UE builds with /fp:fast, which turns a division by the constant 100.0 into a multiplication by 0.01 and lands one ulp
	// away for some values (see FrmFactoryMetres in FrmFactory.cpp). The volatile divisor keeps the true division.
	volatile double Hundred = 100.0;
	return JsRound(GameUnits / Hundred);
}

bool SatisDash::InWholeMetreBounds(double WholeMetres)
{
	return FMath::IsFinite(WholeMetres) && FMath::Abs(WholeMetres) <= 1000000.0;
}

bool SatisDash::ReadFrmLocation(FTokenReader& Reader, EJsonNotation Started, double& OutX, double& OutY)
{
	if (Started != EJsonNotation::ObjectStart)
	{
		return false;
	}
	uint8 Seen = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			return Seen == 7; // x, y and z
		}
		const FString Key = Reader.Name();
		const bool bX = FrmMapIs(Key, TEXT("x"));
		const bool bY = FrmMapIs(Key, TEXT("y"));
		const bool bZ = FrmMapIs(Key, TEXT("z"));
		if (bX || bY || bZ || FrmMapIs(Key, TEXT("rotation")))
		{
			if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
			{
				return false;
			}
			if (bX)
			{
				OutX = Reader.Number();
				Seen |= 1;
			}
			else if (bY)
			{
				OutY = Reader.Number();
				Seen |= 2;
			}
			else if (bZ)
			{
				Seen |= 4;
			}
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}
