#include "SatisDash/Reply.h"

#include "Misc/ScopeLock.h"

SatisDash::FCappedBody::FCappedBody()
{
	SetIsSaving(true);
}

void SatisDash::FCappedBody::Serialize(void* Data, int64 Length)
{
	FScopeLock Lock(&Guard);
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
	FScopeLock Lock(&Guard);
	return bOverflowed;
}

TArray<uint8> SatisDash::FCappedBody::Take()
{
	FScopeLock Lock(&Guard);
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
	if ((Raw.Code >= 300 && Raw.Code < 400) || !Raw.EffectiveUrl.Equals(RequestUrl, ESearchCase::CaseSensitive))
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
