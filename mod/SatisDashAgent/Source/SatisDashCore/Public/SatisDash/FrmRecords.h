#pragma once

#include "CoreMinimal.h"
#include "SatisDash/JsonTokens.h"

namespace SatisDash
{
	/** FRM bridge spec "Threading": about 2,000 records per worker slice. */
	inline constexpr int32 FrmSliceRecords = 2000;

	/** {"observedAt":"<ObservedAt>","data": : what a world body (worldReader.ts: { observedAt, data }) is written after. */
	SATISDASHCORE_API FString WorldBodyPrefix(const FString& ObservedAt);

	/**
	 * An FRM body that is an array of objects, read a slice of records at a time (FRM bridge spec "Memory": no JSON tree) and
	 * written straight to compact JSON text. A subclass reads one record's object (ReadRecord), validates it against the Node
	 * agent's zod schema, maps it, and appends its JSON; this class owns the array, the slices, the caps and the counts.
	 *
	 *  - Every record is read and validated, even past the cap (zod validates the whole array before the Node agent cuts it): an
	 *    invalid record anywhere makes the step Invalid and the whole part is left out.
	 *  - The output is Prefix + "[" + records + "]" + Suffix. A world body uses Prefix {"observedAt":"...","data": and Suffix }.
	 *  - Written by hand with Json::AppendString and FString::AppendInt, not TJsonWriter, which holds every byte twice (Plan 3B).
	 *
	 * Pure, and touched by one thread at a time (FFrmBridge hands it to the worker a slice at a time). Not copyable: the reader
	 * holds a view of Body.
	 */
	class SATISDASHCORE_API FFrmArrayParse
	{
	public:
		enum class EStep : uint8 { More, Done, Invalid };

		virtual ~FFrmArrayParse() = default;
		FFrmArrayParse(const FFrmArrayParse&) = delete;
		FFrmArrayParse& operator=(const FFrmArrayParse&) = delete;

		/** Reads at most MaxPerSlice more records. After Done or Invalid it does nothing more. */
		EStep Step(int32 MaxPerSlice = FrmSliceRecords);
		/** After Done: the text. Empties it. */
		FString TakeJson();
		/** After Done: the text, not emptied (for hashing it). */
		const FString& PeekJson() const { return Out; }
		/** After Done: where the array's "[" is in PeekJson(), and one past its "]". */
		int32 DataFrom() const { return Prefix.Len(); }
		int32 DataEnd() const { return ArrayEnd; }
		/** After Done: the UTF-8 length of the whole text, measured before it is taken. */
		int64 Utf8Bytes() const { return Bytes; }
		/** Written. */
		int32 Kept() const { return KeptCount; }
		/** Valid but not sent: a bad coordinate, or too few or too many points. */
		int32 Dropped() const { return DroppedCount; }
		/** Valid and sendable but past the cap. */
		int32 Over() const { return OverCount; }

	protected:
		enum class ERecord : uint8 { Invalid, Dropped, Over, Kept };

		FFrmArrayParse(TArray<uint8> InBody, int32 InMaxKept, FString InPrefix, FString InSuffix);
		/**
		 * Called with an ObjectStart just read: reads through its ObjectEnd. Invalid: the record is not what zod accepts.
		 * Dropped: valid, but the mapper drops it. Over: valid and sendable, but !bRoom (nothing is written). Kept: written
		 * (call BeginRecord() first, then append the record's JSON to Out).
		 */
		virtual ERecord ReadRecord(bool bRoom) = 0;
		void BeginRecord();
		static bool Is(const FString& Key, const TCHAR* Name);

		TArray<uint8> Body;
		FTokenReader Reader;
		FString Out;

	private:
		EStep Fail();

		const int32 MaxKept;
		const FString Prefix;
		const FString Suffix;
		EStep State = EStep::More;
		bool bStarted = false;
		int32 KeptCount = 0;
		int32 DroppedCount = 0;
		int32 OverCount = 0;
		int32 ArrayEnd = 0;
		int64 Bytes = 0;
	};
}
