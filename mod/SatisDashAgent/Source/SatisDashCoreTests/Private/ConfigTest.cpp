#include "SatisDash/Config.h"
#include "TestUtil.h"

using namespace SatisDash;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigUrlTest, "SatisDash.Core.Config.BackendUrl", SatisDash::Test::Flags)

bool FConfigUrlTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("https host"), IsAllowedBackendUrl(TEXT("https://api.satis-manager.com")));
	TestTrue(TEXT("https host, port and path"), IsAllowedBackendUrl(TEXT("https://example.com:8443/base")));
	TestTrue(TEXT("http on 127.0.0.1"), IsAllowedBackendUrl(TEXT("http://127.0.0.1:3000")));
	TestTrue(TEXT("http on localhost"), IsAllowedBackendUrl(TEXT("http://LocalHost:3000")));
	TestFalse(TEXT("http elsewhere"), IsAllowedBackendUrl(TEXT("http://example.com")));
	TestFalse(TEXT("http look-alike host"), IsAllowedBackendUrl(TEXT("http://127.0.0.1.evil.example")));
	TestFalse(TEXT("http userinfo trick"), IsAllowedBackendUrl(TEXT("http://localhost@evil.example")));
	TestFalse(TEXT("https userinfo"), IsAllowedBackendUrl(TEXT("https://user:pw@example.com")));
	TestFalse(TEXT("no scheme"), IsAllowedBackendUrl(TEXT("example.com")));
	TestFalse(TEXT("another scheme"), IsAllowedBackendUrl(TEXT("ftp://example.com")));
	TestFalse(TEXT("no host"), IsAllowedBackendUrl(TEXT("https://")));
	TestFalse(TEXT("query"), IsAllowedBackendUrl(TEXT("https://example.com/?x=1")));
	TestFalse(TEXT("fragment"), IsAllowedBackendUrl(TEXT("https://example.com/#x")));
	TestFalse(TEXT("whitespace"), IsAllowedBackendUrl(TEXT("https://exa mple.com")));
	TestFalse(TEXT("backslash"), IsAllowedBackendUrl(TEXT("https://example.com\\x")));
	TestFalse(TEXT("port out of range"), IsAllowedBackendUrl(TEXT("https://example.com:99999")));
	TestFalse(TEXT("empty port"), IsAllowedBackendUrl(TEXT("https://example.com:")));

	// The transport's check on every request URL (Task 1e): the backend part as above, then the mod's own path and query.
	bool bPlainHttp = true;
	TestTrue(TEXT("request: https"), IsAllowedRequestUrl(TEXT("https://example.com/agent/v1/snapshots"), bPlainHttp));
	TestFalse(TEXT("request: https is not plain http"), bPlainHttp);
	TestTrue(TEXT("request: the poll's query"), IsAllowedRequestUrl(TEXT("https://example.com/agent/v1/commands?waitSeconds=25"), bPlainHttp));
	TestTrue(TEXT("request: http on 127.0.0.1"), IsAllowedRequestUrl(TEXT("http://127.0.0.1:3000/agent/v1/enroll"), bPlainHttp));
	TestTrue(TEXT("request: http on loopback is plain http"), bPlainHttp);
	TestTrue(TEXT("request: HTTP on localhost"), IsAllowedRequestUrl(TEXT("HTTP://localhost:3000/agent/v1/enroll"), bPlainHttp));
	TestTrue(TEXT("request: the scheme compares without case"), bPlainHttp);
	TestFalse(TEXT("request: http elsewhere"), IsAllowedRequestUrl(TEXT("http://example.com/agent/v1/enroll"), bPlainHttp));
	TestFalse(TEXT("request: http look-alike"), IsAllowedRequestUrl(TEXT("http://127.0.0.1.evil.example/agent/v1/enroll"), bPlainHttp));
	TestFalse(TEXT("request: a query can't hide a host"), IsAllowedRequestUrl(TEXT("http://example.com?@127.0.0.1"), bPlainHttp));
	TestFalse(TEXT("request: fragment in the query"), IsAllowedRequestUrl(TEXT("https://example.com/a?x=1#y"), bPlainHttp));
	TestFalse(TEXT("request: whitespace in the query"), IsAllowedRequestUrl(TEXT("https://example.com/a?x=1 2"), bPlainHttp));
	TestFalse(TEXT("request: another scheme"), IsAllowedRequestUrl(TEXT("ftp://example.com/a"), bPlainHttp));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigParseTest, "SatisDash.Core.Config.Parse", SatisDash::Test::Flags)

bool FConfigParseTest::RunTest(const FString& Parameters)
{
	const FConfigResult Ok = ParseConfig(TEXT("{\"backendUrl\":\"https://example.com/\",\"enrollmentCode\":\" ab3d-7xq2-k5mn\"}"));
	TestTrue(TEXT("valid"), Ok.Status == EConfigStatus::Valid);
	SatisDash::Test::SameText(*this, TEXT("trailing slash dropped"), Ok.Config.BackendUrl, TEXT("https://example.com"));
	SatisDash::Test::SameText(*this, TEXT("code trimmed and upper-cased"), Ok.Config.EnrollmentCode, TEXT("AB3D-7XQ2-K5MN"));

	// The backend issues and accepts only ^[A-Z2-7]{4}-[A-Z2-7]{4}-[A-Z2-7]{4}$ (dash #524, ADR-0046).
	TestTrue(TEXT("code: 12 base32 characters"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5MN")));
	TestTrue(TEXT("code: every base32 digit"), IsEnrollmentCode(TEXT("ZZ22-3456-7777")));
	TestFalse(TEXT("code: the old 8-character form"), IsEnrollmentCode(TEXT("AB3D-7XQ2")));
	TestFalse(TEXT("code: 11 characters"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5M")));
	TestFalse(TEXT("code: 13 characters"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5MNA")));
	TestFalse(TEXT("code: lower case"), IsEnrollmentCode(TEXT("ab3d-7xq2-k5mn")));
	TestFalse(TEXT("code: a 0"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5M0")));
	TestFalse(TEXT("code: a 1"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5M1")));
	TestFalse(TEXT("code: an 8"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5M8")));
	TestFalse(TEXT("code: a 9"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5M9")));
	TestFalse(TEXT("code: no dashes"), IsEnrollmentCode(TEXT("AB3D7XQ2K5MN")));
	TestFalse(TEXT("code: dashes in the wrong places"), IsEnrollmentCode(TEXT("AB3-D7XQ-2K5MN")));
	TestFalse(TEXT("code: a dash in place of a character"), IsEnrollmentCode(TEXT("AB3D-7XQ2-K5M-")));
	TestFalse(TEXT("code: empty"), IsEnrollmentCode(FString()));
	TestFalse(TEXT("code: the old form still fails in a config file"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB3D-7XQ2\"}")).Status == EConfigStatus::Valid);

	TestTrue(TEXT("no code field is an empty code"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://example.com\"}")).Config.EnrollmentCode.IsEmpty());
	TestTrue(TEXT("unknown fields ignored"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://example.com\",\"x\":1}")).Status == EConfigStatus::Valid);

	auto expectBroken = [this](const TCHAR* Text, const TCHAR* Field)
	{
		const FConfigResult R = ParseConfig(Text);
		TestTrue(FString::Printf(TEXT("broken: %s"), Text), R.Status == EConfigStatus::Broken);
		SatisDash::Test::SameText(*this, TEXT("bad field"), R.BadField, Field);
	};
	expectBroken(TEXT("not json"), TEXT("json"));
	expectBroken(TEXT("[]"), TEXT("json"));
	expectBroken(TEXT("{}"), TEXT("backendUrl"));
	expectBroken(TEXT("{\"backendUrl\":5}"), TEXT("backendUrl"));
	expectBroken(TEXT("{\"backendUrl\":\"http://example.com\"}"), TEXT("backendUrl"));
	expectBroken(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB3D7XQ2K5MN\"}"), TEXT("enrollmentCode"));
	expectBroken(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB1D-7XQ2\"}"), TEXT("enrollmentCode"));
	expectBroken(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":12}"), TEXT("enrollmentCode"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigFileTest, "SatisDash.Core.Config.File", SatisDash::Test::Flags)

bool FConfigFileTest::RunTest(const FString& Parameters)
{
	const SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.Path / TEXT("SatisDashAgent.cfg");

	const FConfigResult Missing = LoadConfigFile(Path);
	TestTrue(TEXT("a missing file is reported"), Missing.Status == EConfigStatus::Missing);
	SatisDash::Test::SameText(*this, TEXT("default url"), Missing.Config.BackendUrl, DefaultBackendUrl);
	const FConfigResult Written = ParseConfig(SatisDash::Test::ReadText(Path));
	TestTrue(TEXT("the default file is written and valid"), Written.Status == EConfigStatus::Valid);
	TestTrue(TEXT("with an empty code"), Written.Config.EnrollmentCode.IsEmpty());

	SatisDash::Test::WriteText(Path, TEXT("{ broken"));
	TestTrue(TEXT("broken"), LoadConfigFile(Path).Status == EConfigStatus::Broken);
	SatisDash::Test::SameText(*this, TEXT("a broken file is left as it is"), SatisDash::Test::ReadText(Path), TEXT("{ broken"));

	// Notepad: a UTF-8 BOM and CRLF line ends.
	FFileHelper::SaveStringToFile(TEXT("{\r\n  \"backendUrl\": \"https://example.com\",\r\n  \"enrollmentCode\": \"AB3D-7XQ2-K5MN\"\r\n}\r\n"),
		*Path, FFileHelper::EEncodingOptions::ForceUTF8);
	const FConfigResult Edited = LoadConfigFile(Path);
	TestTrue(TEXT("BOM and CRLF are read"), Edited.Status == EConfigStatus::Valid);

	TestTrue(TEXT("blanking succeeds"), BlankConfigCode(Path, Edited.Config));
	const FConfigResult Blanked = LoadConfigFile(Path);
	TestTrue(TEXT("the code is blanked"), Blanked.Config.EnrollmentCode.IsEmpty());
	SatisDash::Test::SameText(*this, TEXT("the url is kept"), Blanked.Config.BackendUrl, TEXT("https://example.com"));
	TestFalse(TEXT("no temp file left"), IFileManager::Get().FileExists(*(Path + TEXT(".tmp"))));

	TestTrue(TEXT("a missing file's time"), ConfigModTime(Dir.Path / TEXT("nope.cfg")) == FDateTime::MinValue());
	TestTrue(TEXT("an existing file's time"), ConfigModTime(Path) > FDateTime::MinValue());
	return true;
}
