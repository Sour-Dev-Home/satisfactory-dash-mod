#pragma once

#include "CoreMinimal.h"
#include "SatisDash/FrmFactory.h"

/**
 * The building rules the native factory part and the native extractors list share (ADR-0058), so a machine and an extractor
 * are bounded and converted the same way: finite checks, non-negative clamps, the location in metres, the clock in percent.
 * Moved here unchanged from NativeFactory.cpp's anonymous namespace (S4-B).
 */
namespace SatisDash::NativeShared
{
	/** By the exponent bits: UE builds with /fp:fast, where the compiler may assume no infinity and fold isfinite away. */
	inline bool Finite(double Value)
	{
		uint64 Bits = 0;
		FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
		return (Bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
	}

	/** Zero for anything the bridge's schema would refuse: negative, -0 or not finite. */
	inline double NonNegative(double Value)
	{
		return Finite(Value) && Value > 0 ? Value : 0.0;
	}

	/** Sets the model's location (metres, yaw in [0, 360)) when all four values are finite; false leaves it out. */
	inline bool SetLocation(FFactoryBuildingModel& Model, double XCm, double YCm, double ZCm, double YawDeg)
	{
		if (!(Finite(XCm) && Finite(YCm) && Finite(ZCm) && Finite(YawDeg)))
		{
			return false;
		}
		Model.bHasLocation = true;
		Model.XM = FrmFactoryMetres(XCm);
		Model.YM = FrmFactoryMetres(YCm);
		Model.ZM = FrmFactoryMetres(ZCm);
		Model.YawDeg = FrmFactoryYaw(YawDeg);
		return true;
	}

	/** The clock in percent to 0.01 by a true division, as the metres are, so 159.99999 is 160. Anything non-finite along the way (a huge value overflows when scaled), or nothing positive left, is not reported. */
	inline TOptional<double> ClockPercent(double Percent)
	{
		if (!Finite(Percent))
		{
			return TOptional<double>();
		}
		const double Scaled = FMath::RoundToDouble(Percent * 100.0);
		const double Hundredths = Finite(Scaled) ? FrmFactoryMetres(Scaled) : 0.0;
		if (Finite(Hundredths) && Hundredths > 0)
		{
			return Hundredths;
		}
		return TOptional<double>();
	}

	/** Ordinal order of two texts. */
	inline int32 CompareText(const FString& A, const FString& B)
	{
		return A.Compare(B, ESearchCase::CaseSensitive);
	}
}
