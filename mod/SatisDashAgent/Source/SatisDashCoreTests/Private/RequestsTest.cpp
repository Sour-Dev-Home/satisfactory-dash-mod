#include "SatisDash/JsonUtil.h"
#include "SatisDash/Requests.h"
#include "SatisDash/Version.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	FAgentConfig Config()
	{
		FAgentConfig C;
		C.BackendUrl = TEXT("https://example.com/base");
		C.EnrollmentCode = TEXT("AB3D-7XQ2");
		return C;
	}

	FCredential Credential()
	{
		FCredential C;
		C.AgentSecret = SatisDash::Test::FakeSecret();
		C.ServerId = TEXT("srv-1");
		return C;
	}

	FString Header(const FRequestSpec& Spec, const TCHAR* Name)
	{
		for (const TPair<FString, FString>& H : Spec.Headers)
		{
			if (H.Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return H.Value;
			}
		}
		return FString();
	}

	FString BodyText(const FRequestSpec& Spec)
	{
		const FUTF8ToTCHAR Converted = FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Spec.Body.GetData()), Spec.Body.Num());
		return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsEnrollTest, "SatisDash.Core.Requests.Enroll", SatisDash::Test::Flags)

bool FRequestsEnrollTest::RunTest(const FString& Parameters)
{
	const FRequestSpec Spec = BuildEnrollRequest(Config());
	SatisDash::Test::SameText(*this, TEXT("verb"), Spec.Verb, TEXT("POST"));
	SatisDash::Test::SameText(*this, TEXT("url"), Spec.Url, TEXT("https://example.com/base/agent/v1/enroll"));
	TestTrue(TEXT("no Authorization on enrol"), Header(Spec, TEXT("Authorization")).IsEmpty());
	SatisDash::Test::SameText(*this, TEXT("content type"), Header(Spec, TEXT("Content-Type")), TEXT("application/json"));
	SatisDash::Test::SameText(*this, TEXT("user agent: the mod's version only"), Header(Spec, TEXT("User-Agent")), TEXT("satisfactory-dash-") + AgentVersion());
	const TSharedPtr<FJsonObject> Body = SatisDash::Json::ParseObject(BodyText(Spec));
	TestTrue(TEXT("body is JSON"), Body.IsValid());
	if (Body.IsValid())
	{
		SatisDash::Test::SameText(*this, TEXT("code"), Body->GetStringField(TEXT("code")), TEXT("AB3D-7XQ2"));
		SatisDash::Test::SameText(*this, TEXT("agentVersion"), Body->GetStringField(TEXT("agentVersion")), AgentVersion());
		TestEqual(TEXT("exactly two fields (EnrollRequestSchema is strict)"), Body->Values.Num(), 2);
	}
	TestEqual(TEXT("timeout"), Spec.TimeoutSeconds, RequestTimeoutSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsAuthTest, "SatisDash.Core.Requests.Authenticated", SatisDash::Test::Flags)

bool FRequestsAuthTest::RunTest(const FString& Parameters)
{
	const FString Bearer = TEXT("Bearer ") + SatisDash::Test::FakeSecret();
	const FRequestSpec Snapshot = BuildSnapshotRequest(Config(), Credential(), TEXT("{\"a\":1}"));
	const FRequestSpec Poll = BuildCommandsPoll(Config(), Credential());
	const FRequestSpec Result = BuildResultRequest(Config(), Credential(), TEXT("a/b?c"), CommandFailed(TEXT("upstream_error")));

	SatisDash::Test::SameText(*this, TEXT("snapshot url"), Snapshot.Url, TEXT("https://example.com/base/agent/v1/snapshots"));
	SatisDash::Test::SameText(*this, TEXT("poll verb"), Poll.Verb, TEXT("GET"));
	SatisDash::Test::SameText(*this, TEXT("poll url"), Poll.Url, TEXT("https://example.com/base/agent/v1/commands?waitSeconds=25"));
	TestEqual(TEXT("poll timeout: the wait plus grace"), Poll.TimeoutSeconds, 35.f);
	TestEqual(TEXT("poll has no body"), Poll.Body.Num(), 0);
	SatisDash::Test::SameText(*this, TEXT("result url, id percent-encoded"), Result.Url, TEXT("https://example.com/base/agent/v1/commands/a%2Fb%3Fc/result"));
	SatisDash::Test::SameText(*this, TEXT("result body"), BodyText(Result), TEXT("{\"ok\":false,\"code\":\"upstream_error\"}"));

	for (const FRequestSpec* Spec : { &Snapshot, &Poll, &Result })
	{
		SatisDash::Test::SameText(*this, TEXT("Authorization header"), Header(*Spec, TEXT("Authorization")), Bearer);
		TestFalse(TEXT("the secret is never in a URL (S7)"), Spec->Url.Contains(SatisDash::Test::FakeSecret(), ESearchCase::CaseSensitive));
		SatisDash::Test::SameText(*this, TEXT("accept"), Header(*Spec, TEXT("Accept")), TEXT("application/json"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsGzipTest, "SatisDash.Core.Requests.Gzip", SatisDash::Test::Flags)

bool FRequestsGzipTest::RunTest(const FString& Parameters)
{
	auto ofBytes = [](int32 Bytes) { return TEXT("{\"p\":\"") + FString::ChrN(Bytes - 8, TEXT('x')) + TEXT("\"}"); };
	const FRequestSpec Small = BuildSnapshotRequest(Config(), Credential(), ofBytes(1024));
	TestEqual(TEXT("1024 bytes: sent as is"), Small.Body.Num(), 1024);
	TestTrue(TEXT("no Content-Encoding"), Header(Small, TEXT("Content-Encoding")).IsEmpty());

	const FRequestSpec Big = BuildSnapshotRequest(Config(), Credential(), ofBytes(1025));
	SatisDash::Test::SameText(*this, TEXT("1025 bytes: gzipped"), Header(Big, TEXT("Content-Encoding")), TEXT("gzip"));
	TestTrue(TEXT("gzip magic bytes"), Big.Body.Num() > 2 && Big.Body[0] == 0x1f && Big.Body[1] == 0x8b);
	TestTrue(TEXT("smaller than the input"), Big.Body.Num() < 1025);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsEncodedBodyTest, "SatisDash.Core.Requests.EncodedBody", SatisDash::Test::Flags)

bool FRequestsEncodedBodyTest::RunTest(const FString& Parameters)
{
	// The pusher stores the encoded body; what goes out must be exactly what the JSON overload sends.
	for (const int32 Size : { 100, 3000 })
	{
		const FString Json = TEXT("{\"p\":\"") + FString::ChrN(Size - 8, TEXT('x')) + TEXT("\"}");
		const FEncodedBody Encoded = EncodeBody(Json);
		TestTrue(TEXT("gzipped only above 1 KB"), Encoded.bGzip == (Size > GzipOverBytes));
		const FRequestSpec FromJson = BuildSnapshotRequest(Config(), Credential(), Json);
		const FRequestSpec FromBody = BuildSnapshotRequest(Config(), Credential(), Encoded);
		TestTrue(TEXT("the same bytes"), FromJson.Body == FromBody.Body);
		TestEqual(TEXT("the same number of headers"), FromBody.Headers.Num(), FromJson.Headers.Num());
		for (int32 I = 0; I < FromJson.Headers.Num() && I < FromBody.Headers.Num(); ++I)
		{
			SatisDash::Test::SameText(*this, TEXT("header name"), FromBody.Headers[I].Key, FromJson.Headers[I].Key);
			SatisDash::Test::SameText(*this, TEXT("header value"), FromBody.Headers[I].Value, FromJson.Headers[I].Value);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRequestsEncodeTest, "SatisDash.Core.Requests.PercentEncode", SatisDash::Test::Flags)

bool FRequestsEncodeTest::RunTest(const FString& Parameters)
{
	SatisDash::Test::SameText(*this, TEXT("unreserved kept"), PercentEncode(TEXT("AZaz09-._~")), TEXT("AZaz09-._~"));
	SatisDash::Test::SameText(*this, TEXT("space"), PercentEncode(TEXT("a b")), TEXT("a%20b"));
	SatisDash::Test::SameText(*this, TEXT("UTF-8"), PercentEncode(TEXT("é")), TEXT("%C3%A9"));
	return true;
}
