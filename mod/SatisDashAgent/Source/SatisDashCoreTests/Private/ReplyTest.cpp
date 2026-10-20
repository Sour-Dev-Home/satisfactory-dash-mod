#include "SatisDash/Reply.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	void feed(FCappedBody& CappedBody, int32 Bytes)
	{
		TArray<uint8> Chunk;
		Chunk.SetNumZeroed(Bytes);
		CappedBody.Serialize(Chunk.GetData(), Chunk.Num());
	}

	FRawReply answered(const TCHAR* Url)
	{
		FRawReply Raw;
		Raw.bProcessed = true;
		Raw.Code = 200;
		Raw.ContentType = TEXT("application/json; charset=utf-8");
		Raw.RetryAfter = TEXT("7");
		Raw.EffectiveUrl = Url;
		return Raw;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplyCappedBodyTest, "SatisDash.Core.Reply.CappedBody", SatisDash::Test::Flags)

bool FReplyCappedBodyTest::RunTest(const FString& Parameters)
{
	{
		FCappedBody CappedBody;
		feed(CappedBody, MaxResponseBytes - 1);
		feed(CappedBody, 1);
		TestFalse(TEXT("exactly 64 KB fits"), CappedBody.Overflowed());
		TestFalse(TEXT("and is no error"), CappedBody.IsError());
		TestEqual(TEXT("all of it kept"), CappedBody.Take().Num(), MaxResponseBytes);
	}
	{
		FCappedBody CappedBody;
		feed(CappedBody, MaxResponseBytes);
		feed(CappedBody, 1);
		TestTrue(TEXT("one byte more overflows"), CappedBody.Overflowed());
		TestTrue(TEXT("and fails the request"), CappedBody.IsError());
		TestTrue(TEXT("nothing past the cap is kept"), CappedBody.Take().Num() <= MaxResponseBytes);
	}
	{
		FCappedBody CappedBody;
		feed(CappedBody, MaxResponseBytes * 4);
		TestTrue(TEXT("one huge chunk overflows at once"), CappedBody.Overflowed());
		TestEqual(TEXT("and keeps nothing of it"), CappedBody.Take().Num(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplyMakeTest, "SatisDash.Core.Reply.Make", SatisDash::Test::Flags)

bool FReplyMakeTest::RunTest(const FString& Parameters)
{
	const FString Url = TEXT("https://example.com/agent/v1/snapshots");
	{
		FCappedBody CappedBody;
		feed(CappedBody, 3);
		EReplyRefusal Refusal = EReplyRefusal::Oversized;
		const FHttpReply Reply = MakeReply(answered(*Url), Url, CappedBody, Refusal);
		TestTrue(TEXT("a direct answer is kept"), Refusal == EReplyRefusal::None);
		TestEqual(TEXT("its code"), Reply.Code, 200);
		SatisDash::Test::SameText(*this, TEXT("its content type"), Reply.ContentType, TEXT("application/json; charset=utf-8"));
		SatisDash::Test::SameText(*this, TEXT("its Retry-After"), Reply.RetryAfter, TEXT("7"));
		TestEqual(TEXT("its body"), Reply.Body.Num(), 3);
	}
	{
		// S3: curl followed a redirect; the answer came from somewhere else and is refused.
		FCappedBody CappedBody;
		EReplyRefusal Refusal = EReplyRefusal::None;
		const FHttpReply Reply = MakeReply(answered(TEXT("https://example.com:4443/agent/v1/snapshots")), Url, CappedBody, Refusal);
		TestTrue(TEXT("redirected"), Refusal == EReplyRefusal::Redirected);
		TestEqual(TEXT("is no answer"), Reply.Code, 0);
	}
	{
		// S3: the transport never follows a redirect (Task 1e), so a 3xx arrives from the URL asked; it is refused too.
		for (const int32 Code : { 300, 301, 302, 303, 307, 308, 399 })
		{
			FCappedBody CappedBody;
			FRawReply Raw = answered(*Url);
			Raw.Code = Code;
			EReplyRefusal Refusal = EReplyRefusal::None;
			TestEqual(FString::Printf(TEXT("%d: no answer"), Code), MakeReply(Raw, Url, CappedBody, Refusal).Code, 0);
			TestTrue(FString::Printf(TEXT("%d: as Redirected"), Code), Refusal == EReplyRefusal::Redirected);
		}
		for (const int32 Code : { 204, 299, 400, 401 })
		{
			FCappedBody CappedBody;
			FRawReply Raw = answered(*Url);
			Raw.Code = Code;
			EReplyRefusal Refusal = EReplyRefusal::Redirected;
			TestEqual(FString::Printf(TEXT("%d: passed on"), Code), MakeReply(Raw, Url, CappedBody, Refusal).Code, Code);
			TestTrue(FString::Printf(TEXT("%d: not refused"), Code), Refusal == EReplyRefusal::None);
		}
	}
	{
		// libcurl reports the scheme and host as it normalised them; their case is not a different place (Task 1e review).
		FCappedBody CappedBody;
		EReplyRefusal Refusal = EReplyRefusal::Redirected;
		MakeReply(answered(TEXT("https://example.com/agent/v1/snapshots")), TEXT("HTTPS://Example.COM/agent/v1/snapshots"), CappedBody, Refusal);
		TestTrue(TEXT("scheme and host compare without case"), Refusal == EReplyRefusal::None);
		MakeReply(answered(TEXT("https://example.com:4443/agent/v1/snapshots")), TEXT("HTTPS://Example.COM/agent/v1/snapshots"), CappedBody, Refusal);
		TestTrue(TEXT("but another port is still another place"), Refusal == EReplyRefusal::Redirected);
		MakeReply(answered(TEXT("https://example.com/agent/v1/Snapshots")), TEXT("HTTPS://Example.COM/agent/v1/snapshots"), CappedBody, Refusal);
		TestTrue(TEXT("and the path still compares exactly"), Refusal == EReplyRefusal::Redirected);
	}
	{
		// The URL compares exactly: a case change in the path is another resource.
		FCappedBody CappedBody;
		EReplyRefusal Refusal = EReplyRefusal::None;
		MakeReply(answered(TEXT("https://example.com/agent/v1/Snapshots")), Url, CappedBody, Refusal);
		TestTrue(TEXT("case-sensitive"), Refusal == EReplyRefusal::Redirected);
	}
	{
		FCappedBody CappedBody;
		FRawReply Raw = answered(*Url);
		Raw.bProcessed = false;
		EReplyRefusal Refusal = EReplyRefusal::None;
		TestEqual(TEXT("not processed: no answer"), MakeReply(Raw, Url, CappedBody, Refusal).Code, 0);
		TestTrue(TEXT("as NoAnswer"), Refusal == EReplyRefusal::NoAnswer);
	}
	{
		// S4: the cap wins even when UE calls the request processed.
		FCappedBody CappedBody;
		feed(CappedBody, MaxResponseBytes + 1);
		EReplyRefusal Refusal = EReplyRefusal::None;
		TestEqual(TEXT("oversized: no answer"), MakeReply(answered(*Url), Url, CappedBody, Refusal).Code, 0);
		TestTrue(TEXT("as Oversized"), Refusal == EReplyRefusal::Oversized);
	}
	return true;
}
