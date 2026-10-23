#include "SatisDash/Reply.h"

namespace
{
	/** Where the path starts: after scheme://authority, or the end when there is no path. */
	int32 pathStart(const FString& Url)
	{
		const int32 HostAt = Url.Find(TEXT("://"), ESearchCase::CaseSensitive);
		const int32 PathAt = Url.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HostAt == INDEX_NONE ? 0 : HostAt + 3);
		return PathAt == INDEX_NONE ? Url.Len() : PathAt;
	}

	/** The same place: scheme and authority compare without case (libcurl reports them normalised), the rest exactly. */
	bool samePlace(const FString& Effective, const FString& Requested)
	{
		const int32 EffectivePath = pathStart(Effective);
		const int32 RequestedPath = pathStart(Requested);
		return Effective.Left(EffectivePath).Equals(Requested.Left(RequestedPath), ESearchCase::IgnoreCase)
			&& Effective.RightChop(EffectivePath).Equals(Requested.RightChop(RequestedPath), ESearchCase::CaseSensitive);
	}
}

SatisDash::FCappedBody::FCappedBody()
{
	SetIsSaving(true);
}

void SatisDash::FCappedBody::Serialize(void* Data, int64 Length)
{
	if (bOverflowed || Length < 0 || Bytes.Num() + Length > MaxResponseBytes)
	{
		bOverflowed = true;
		SetError();
		return;
	}
	Bytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));
}

bool SatisDash::FCappedBody::Overflowed() const
{
	return bOverflowed;
}

TArray<uint8> SatisDash::FCappedBody::Take()
{
	return MoveTemp(Bytes);
}

SatisDash::FHttpReply SatisDash::MakeReply(const FRawReply& Raw, const FString& RequestUrl, FCappedBody& CappedBody, EReplyRefusal& OutRefusal)
{
	FHttpReply Reply;
	if (CappedBody.Overflowed())
	{
		OutRefusal = EReplyRefusal::Oversized;
		return Reply;
	}
	if (!Raw.bProcessed)
	{
		OutRefusal = EReplyRefusal::NoAnswer;
		return Reply;
	}
	// S3: the transport never follows a redirect, so a 3xx answer is the redirect itself; an effective URL other than the
	// one asked would mean one was followed anyway. Either way the answer is refused.
	if ((Raw.Code >= 300 && Raw.Code < 400) || !samePlace(Raw.EffectiveUrl, RequestUrl))
	{
		OutRefusal = EReplyRefusal::Redirected;
		return Reply;
	}
	OutRefusal = EReplyRefusal::None;
	Reply.Code = Raw.Code;
	Reply.ContentType = Raw.ContentType;
	Reply.RetryAfter = Raw.RetryAfter;
	Reply.Body = CappedBody.Take();
	return Reply;
}
