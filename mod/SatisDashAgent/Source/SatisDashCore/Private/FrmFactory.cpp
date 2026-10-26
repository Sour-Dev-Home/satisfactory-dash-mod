#include "SatisDash/FrmFactory.h"

#include "SatisDash/Snapshot.h"

namespace
{
	bool FrmFactoryIs(const FString& Key, const TCHAR* Name)
	{
		return Key.Equals(Name, ESearchCase::CaseSensitive);
	}

	/** zod's .int(): a whole, safe integer. */
	bool FrmFactoryWhole(double Value)
	{
		return FMath::IsFinite(Value) && Value >= -9007199254740991.0 && Value <= 9007199254740991.0 && double(int64(Value)) == Value;
	}
}

/**
 * Centimetres to metres, a true division as JavaScript does it. UE builds with /fp:fast, which turns a division by the
 * constant 100.0 into a multiplication by 0.01 and lands one ulp away for some values (4800.0126953125 / 100); the
 * volatile divisor keeps the compiler from seeing a constant, so the output equals the Node agent's to the bit.
 */
double SatisDash::FrmFactoryMetres(double Centimetres)
{
	volatile double Hundred = 100.0;
	return Centimetres / Hundred;
}

/** conform.ts normalizeYaw: into [0, 360); float noise that lands on 360 is 0. JavaScript's % is fmod. */
double SatisDash::FrmFactoryYaw(double Degrees)
{
	const double Yaw = FMath::Fmod(FMath::Fmod(Degrees, 360.0) + 360.0, 360.0);
	return Yaw >= 360.0 ? 0.0 : Yaw;
}

SatisDash::FFactoryParse::FFactoryParse(TArray<uint8> InBody)
	: Body{MoveTemp(InBody)}
	, Reader{Body}
	, Writer{TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out)}
{
}

SatisDash::FFactoryParse::EStep SatisDash::FFactoryParse::Fail()
{
	State = EStep::Invalid;
	Out.Empty();
	return State;
}

SatisDash::FFactoryParse::EStep SatisDash::FFactoryParse::Step(int32 MaxPerSlice)
{
	if (State != EStep::More)
	{
		return State;
	}
	EJsonNotation N;
	if (!bStarted)
	{
		bStarted = true;
		if (!Reader.Next(N) || N != EJsonNotation::ArrayStart)
		{
			return Fail();
		}
		Writer->WriteObjectStart();
		Writer->WriteArrayStart(TEXT("buildings"));
	}
	for (int32 Done = 0; Done < MaxPerSlice; ++Done)
	{
		if (!Reader.Next(N))
		{
			return Fail();
		}
		if (N == EJsonNotation::ArrayEnd)
		{
			if (!Reader.AtCleanEnd())
			{
				return Fail();
			}
			Writer->WriteArrayEnd();
			Writer->WriteObjectEnd();
			Writer->Close();
			State = EStep::Done;
			return State;
		}
		FBuilding B;
		if (N != EJsonNotation::ObjectStart || !ReadBuilding(B))
		{
			return Fail();
		}
		if (Count < MaxBuildings)
		{
			WriteBuilding(B);
			++Count;
		}
		else
		{
			++Over; // still read and checked: zod validates the whole array before conform cuts it
		}
	}
	return EStep::More;
}

FString SatisDash::FFactoryParse::TakeJson()
{
	return State == EStep::Done ? MoveTemp(Out) : FString();
}

bool SatisDash::FFactoryParse::ReadNonNegative(EJsonNotation N, double& OutValue) const
{
	if (N != EJsonNotation::Number || !IsNonNegativeNoise(Reader.Number()))
	{
		return false;
	}
	OutValue = FMath::Max(0.0, Reader.Number());
	return true;
}

bool SatisDash::FFactoryParse::ReadBuilding(FBuilding& B)
{
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			return B.Id.IsSet() && !B.Id->IsEmpty() && B.Name.IsSet() && B.ClassName.IsSet() && B.IsProducing.IsSet() && B.IsPaused.IsSet();
		}
		const FString& Key = Reader.Name();
		if (FrmFactoryIs(Key, TEXT("ID")) || FrmFactoryIs(Key, TEXT("Name")) || FrmFactoryIs(Key, TEXT("ClassName")) || FrmFactoryIs(Key, TEXT("Recipe")))
		{
			if (N != EJsonNotation::String)
			{
				return false;
			}
			TOptional<FString>& Field = FrmFactoryIs(Key, TEXT("ID")) ? B.Id : FrmFactoryIs(Key, TEXT("Name")) ? B.Name : FrmFactoryIs(Key, TEXT("ClassName")) ? B.ClassName : B.Recipe;
			Field = Reader.String();
			if (&Field == &B.Recipe && B.Recipe->IsEmpty())
			{
				return false; // Recipe: z.string().min(1)
			}
		}
		else if (FrmFactoryIs(Key, TEXT("IsConfigured")) || FrmFactoryIs(Key, TEXT("IsProducing")) || FrmFactoryIs(Key, TEXT("IsPaused")))
		{
			if (N != EJsonNotation::Boolean)
			{
				return false;
			}
			(FrmFactoryIs(Key, TEXT("IsConfigured")) ? B.IsConfigured : FrmFactoryIs(Key, TEXT("IsProducing")) ? B.IsProducing : B.IsPaused) = Reader.Boolean();
		}
		else if (FrmFactoryIs(Key, TEXT("production")) || FrmFactoryIs(Key, TEXT("ingredients")))
		{
			const bool bIngredients = FrmFactoryIs(Key, TEXT("ingredients"));
			if (!ReadRates(N, bIngredients, bIngredients ? B.Ingredients : B.Production))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("OutputInventory")))
		{
			if (!ReadSlots(N, B.Output))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("InputInventory")))
		{
			TArray<FSlot> Slots;
			if (!ReadSlots(N, Slots))
			{
				return false;
			}
			B.Input = MoveTemp(Slots);
		}
		else if (FrmFactoryIs(Key, TEXT("PowerInfo")))
		{
			if (!ReadPowerInfo(N, B.CircuitGroupId))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("location")))
		{
			if (!ReadLocation(N, B))
			{
				return false;
			}
		}
		else if (FrmFactoryIs(Key, TEXT("ManuSpeed")))
		{
			// z.number().optional().catch(undefined): anything but a finite number is dropped, never a failure.
			B.ManuSpeed.Reset();
			if (N == EJsonNotation::Number && FMath::IsFinite(Reader.Number()))
			{
				B.ManuSpeed = Reader.Number();
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadRates(EJsonNotation Started, bool bIngredients, TArray<FRate>& Rates)
{
	if (Started != EJsonNotation::ArrayStart)
	{
		return false;
	}
	Rates.Reset(); // a repeated key: the last one wins, as JSON.parse
	const TCHAR* CurrentKey = bIngredients ? TEXT("CurrentConsumed") : TEXT("CurrentProd");
	const TCHAR* MaxKey = bIngredients ? TEXT("MaxConsumed") : TEXT("MaxProd");
	const TCHAR* PercentKey = bIngredients ? TEXT("ConsPercent") : TEXT("ProdPercent");
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ArrayEnd)
		{
			return true;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return false;
		}
		FRate Rate;
		uint8 Seen = 0;
		while (true)
		{
			if (!Reader.Next(N))
			{
				return false;
			}
			if (N == EJsonNotation::ObjectEnd)
			{
				break;
			}
			const FString& Key = Reader.Name();
			double Amount = 0;
			if (FrmFactoryIs(Key, TEXT("Name")) || FrmFactoryIs(Key, TEXT("ClassName")))
			{
				if (N != EJsonNotation::String)
				{
					return false;
				}
				(FrmFactoryIs(Key, TEXT("Name")) ? Rate.Name : Rate.ClassName) = Reader.String();
				Seen |= FrmFactoryIs(Key, TEXT("Name")) ? 1 : 2;
			}
			else if (FrmFactoryIs(Key, TEXT("Amount")))
			{
				if (!ReadNonNegative(N, Amount))
				{
					return false;
				}
				Seen |= 4;
			}
			else if (FrmFactoryIs(Key, CurrentKey) || FrmFactoryIs(Key, MaxKey) || FrmFactoryIs(Key, PercentKey))
			{
				double& Field = FrmFactoryIs(Key, CurrentKey) ? Rate.Current : FrmFactoryIs(Key, MaxKey) ? Rate.Max : Rate.Percent;
				if (!ReadNonNegative(N, Field))
				{
					return false;
				}
				Seen |= FrmFactoryIs(Key, CurrentKey) ? 8 : FrmFactoryIs(Key, MaxKey) ? 16 : 32;
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		if (Seen != 63)
		{
			return false; // every field of RawFrmProductionItemSchema / RawFrmIngredientItemSchema is required
		}
		Rates.Add(MoveTemp(Rate));
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadSlots(EJsonNotation Started, TArray<FSlot>& Slots)
{
	if (Started != EJsonNotation::ArrayStart)
	{
		return false;
	}
	Slots.Reset();
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ArrayEnd)
		{
			return true;
		}
		if (N != EJsonNotation::ObjectStart)
		{
			return false;
		}
		FSlot Slot;
		uint8 Seen = 0;
		while (true)
		{
			if (!Reader.Next(N))
			{
				return false;
			}
			if (N == EJsonNotation::ObjectEnd)
			{
				break;
			}
			const FString& Key = Reader.Name();
			if (FrmFactoryIs(Key, TEXT("Name")) || FrmFactoryIs(Key, TEXT("ClassName")))
			{
				if (N != EJsonNotation::String)
				{
					return false;
				}
				if (FrmFactoryIs(Key, TEXT("ClassName")))
				{
					Slot.ClassName = Reader.String();
				}
				Seen |= FrmFactoryIs(Key, TEXT("Name")) ? 1 : 2;
			}
			else if (FrmFactoryIs(Key, TEXT("Amount")) || FrmFactoryIs(Key, TEXT("MaxAmount")))
			{
				if (!ReadNonNegative(N, FrmFactoryIs(Key, TEXT("Amount")) ? Slot.Amount : Slot.MaxAmount))
				{
					return false;
				}
				Seen |= FrmFactoryIs(Key, TEXT("Amount")) ? 4 : 8;
			}
			else if (!Reader.SkipValue(N))
			{
				return false;
			}
		}
		if (Seen != 15)
		{
			return false;
		}
		Slots.Add(MoveTemp(Slot));
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadPowerInfo(EJsonNotation Started, TOptional<int64>& OutGroup)
{
	if (Started != EJsonNotation::ObjectStart)
	{
		return false;
	}
	uint8 Seen = 0;
	int64 Group = -1;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			if (Seen != 15)
			{
				return false; // CircuitGroupID, CircuitID, PowerConsumed and MaxPowerConsumed are required
			}
			OutGroup = Group;
			return true;
		}
		const FString& Key = Reader.Name();
		if (FrmFactoryIs(Key, TEXT("CircuitGroupID")) || FrmFactoryIs(Key, TEXT("CircuitID")))
		{
			if (N != EJsonNotation::Number || !FrmFactoryWhole(Reader.Number()))
			{
				return false;
			}
			if (FrmFactoryIs(Key, TEXT("CircuitGroupID")))
			{
				Group = int64(Reader.Number());
			}
			Seen |= FrmFactoryIs(Key, TEXT("CircuitGroupID")) ? 1 : 2;
		}
		else if (FrmFactoryIs(Key, TEXT("PowerConsumed")) || FrmFactoryIs(Key, TEXT("MaxPowerConsumed")))
		{
			if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
			{
				return false;
			}
			Seen |= FrmFactoryIs(Key, TEXT("PowerConsumed")) ? 4 : 8;
		}
		else if (FrmFactoryIs(Key, TEXT("FuseTriggered")))
		{
			if (N != EJsonNotation::Boolean)
			{
				return false;
			}
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}

bool SatisDash::FFactoryParse::ReadLocation(EJsonNotation Started, FBuilding& B)
{
	if (Started != EJsonNotation::ObjectStart)
	{
		return false;
	}
	uint8 Seen = 0;
	B.Rotation = 0;
	EJsonNotation N;
	while (Reader.Next(N))
	{
		if (N == EJsonNotation::ObjectEnd)
		{
			B.bHasLocation = Seen == 7;
			return B.bHasLocation; // x, y and z are required; rotation is optional
		}
		const FString& Key = Reader.Name();
		const bool bAxis = FrmFactoryIs(Key, TEXT("x")) || FrmFactoryIs(Key, TEXT("y")) || FrmFactoryIs(Key, TEXT("z"));
		if (bAxis || FrmFactoryIs(Key, TEXT("rotation")))
		{
			if (N != EJsonNotation::Number || !FMath::IsFinite(Reader.Number()))
			{
				return false;
			}
			double& Field = FrmFactoryIs(Key, TEXT("x")) ? B.X : FrmFactoryIs(Key, TEXT("y")) ? B.Y : FrmFactoryIs(Key, TEXT("z")) ? B.Z : B.Rotation;
			Field = Reader.Number();
			Seen |= FrmFactoryIs(Key, TEXT("x")) ? 1 : FrmFactoryIs(Key, TEXT("y")) ? 2 : FrmFactoryIs(Key, TEXT("z")) ? 4 : 0;
		}
		else if (!Reader.SkipValue(N))
		{
			return false;
		}
	}
	return false;
}

namespace
{
	void FrmFactoryWriteRates(SatisDash::FFactoryJsonWriter& Writer, const TCHAR* Field, const TArray<SatisDash::FRateReading>* Rates)
	{
		Writer.WriteArrayStart(Field);
		const int32 Keep = Rates ? FMath::Min(Rates->Num(), SatisDash::MaxRatesPerBuilding) : 0;
		for (int32 I = 0; I < Keep; ++I)
		{
			const SatisDash::FRateReading& Rate = (*Rates)[I];
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("name"), SatisDash::ClampString(Rate.Name));
			Writer.WriteValue(TEXT("className"), SatisDash::ClampString(Rate.ClassName));
			Writer.WriteValue(TEXT("currentPerMinute"), Rate.Current);
			Writer.WriteValue(TEXT("maxPerMinute"), Rate.Max);
			Writer.WriteValue(TEXT("percent"), Rate.Percent);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}
}

void SatisDash::DeriveFactoryFlags(bool bPaused, bool bConfigured, const TArray<FSlotReading>& Output, const TArray<FRateReading>& Ingredients, const TOptional<TArray<FSlotReading>>& Input, bool& OutBackedUp, bool& OutInputsFull)
{
	// factoryMapper.ts isBackedUp and areInputsFull, on the whole lists (they run before conform cuts them).
	OutBackedUp = false;
	if (!bPaused && bConfigured)
	{
		for (const FSlotReading& Slot : Output)
		{
			OutBackedUp = OutBackedUp || (Slot.MaxAmount > 0 && Slot.Amount >= Slot.MaxAmount);
		}
	}
	OutInputsFull = !bPaused && bConfigured && Ingredients.Num() > 0 && Input.IsSet();
	for (int32 I = 0; OutInputsFull && I < Ingredients.Num(); ++I)
	{
		bool bFull = false;
		for (const FSlotReading& Slot : Input.GetValue())
		{
			bFull = bFull || (Slot.ClassName.Equals(Ingredients[I].ClassName, ESearchCase::CaseSensitive) && Slot.MaxAmount > 0 && Slot.Amount >= Slot.MaxAmount);
		}
		OutInputsFull = bFull;
	}
}

void SatisDash::AppendFactoryBuilding(FFactoryJsonWriter& Writer, const FFactoryBuildingModel& B)
{
	Writer.WriteObjectStart();
	Writer.WriteValue(TEXT("id"), ClampString(B.Id));
	Writer.WriteValue(TEXT("name"), ClampString(B.Name));
	Writer.WriteValue(TEXT("className"), ClampString(B.ClassName));
	if (B.Recipe.IsSet())
	{
		Writer.WriteValue(TEXT("recipe"), ClampString(B.Recipe.GetValue()));
	}
	else
	{
		Writer.WriteNull(TEXT("recipe"));
	}
	Writer.WriteValue(TEXT("isProducing"), B.bProducing);
	Writer.WriteValue(TEXT("isPaused"), B.bPaused);
	Writer.WriteValue(TEXT("isBackedUp"), B.bBackedUp);
	if (B.InputsFull.IsSet())
	{
		Writer.WriteValue(TEXT("inputsFull"), B.InputsFull.GetValue());
	}
	Writer.WriteValue(TEXT("circuitGroupId"), B.CircuitGroupId);
	if (B.bHasLocation)
	{
		Writer.WriteObjectStart(TEXT("location"));
		Writer.WriteValue(TEXT("xM"), B.XM);
		Writer.WriteValue(TEXT("yM"), B.YM);
		Writer.WriteValue(TEXT("zM"), B.ZM);
		Writer.WriteValue(TEXT("rotationDeg"), B.YawDeg);
		Writer.WriteObjectEnd();
	}
	if (B.ClockSpeedPercent.IsSet())
	{
		Writer.WriteValue(TEXT("clockSpeedPercent"), B.ClockSpeedPercent.GetValue());
	}
	FrmFactoryWriteRates(Writer, TEXT("production"), B.Production);
	FrmFactoryWriteRates(Writer, TEXT("ingredients"), B.Ingredients);
	Writer.WriteObjectEnd();
}

void SatisDash::FFactoryParse::WriteBuilding(const FBuilding& B)
{
	// satisfactoryServerAdapter.ts:182-183: "Unassigned" or a missing recipe, or IsConfigured false, is no recipe.
	const bool bConfigured = B.IsConfigured.Get(true) && B.Recipe.IsSet() && !B.Recipe->Equals(TEXT("Unassigned"), ESearchCase::CaseSensitive);
	static const TArray<FRate> NoRates;
	const TArray<FRate>& Production = bConfigured ? B.Production : NoRates;
	const TArray<FRate>& Ingredients = bConfigured ? B.Ingredients : NoRates;
	const bool bPaused = B.IsPaused.GetValue();

	FFactoryBuildingModel Model;
	bool bBackedUp = false;
	bool bInputsFull = false;
	DeriveFactoryFlags(bPaused, bConfigured, B.Output, Ingredients, B.Input, bBackedUp, bInputsFull);

	Model.Id = B.Id.GetValue();
	Model.Name = B.Name.GetValue();
	Model.ClassName = B.ClassName.GetValue();
	if (bConfigured)
	{
		Model.Recipe = B.Recipe;
	}
	Model.bProducing = B.IsProducing.GetValue();
	Model.bPaused = bPaused;
	Model.bBackedUp = bBackedUp;
	if (B.Input.IsSet())
	{
		Model.InputsFull = bInputsFull;
	}
	Model.CircuitGroupId = B.CircuitGroupId.Get(-1);
	if (B.bHasLocation)
	{
		// satisfactoryServerAdapter.ts mapLocation: centimetres to metres, the yaw normalised, the pitch dropped.
		Model.bHasLocation = true;
		Model.XM = FrmFactoryMetres(B.X);
		Model.YM = FrmFactoryMetres(B.Y);
		Model.ZM = FrmFactoryMetres(B.Z);
		Model.YawDeg = FrmFactoryYaw(B.Rotation);
	}
	if (B.ManuSpeed.IsSet() && B.ManuSpeed.GetValue() > 0)
	{
		Model.ClockSpeedPercent = B.ManuSpeed.GetValue();
	}
	Model.Production = &Production;
	Model.Ingredients = &Ingredients;
	AppendFactoryBuilding(*Writer, Model);
}
