#include "SatisDash/Agent.h"
#include "SatisDash/Config.h"
#include "SatisDash/JsonUtil.h"
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
	const FConfigResult Ok = ParseConfig(TEXT("{\"backendUrl\":\"https://example.com/\",\"enrollmentCode\":\" ab3d-7xq2-k5mn \"}"));
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
	expectBroken(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB1D-7XQ2-K5MN\"}"), TEXT("enrollmentCode"));
	expectBroken(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"AB3D-7XQ2\"}"), TEXT("enrollmentCode"));
	expectBroken(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":12}"), TEXT("enrollmentCode"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigNestingTest, "SatisDash.Core.Config.Nesting", SatisDash::Test::Flags)

bool FConfigNestingTest::RunTest(const FString& Parameters)
{
	// Security review F1: a config nested past Json::MaxDepth is broken JSON, like any other unparseable file.
	const FString Head = TEXT("{\"backendUrl\":\"https://example.com\",\"x\":");
	TestTrue(TEXT("an extra field at the cap"),
		ParseConfig(Head + SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth - 1) + TEXT("}")).Status == EConfigStatus::Valid);
	const FConfigResult Deep = ParseConfig(Head + SatisDash::Test::NestedObjects(SatisDash::Json::MaxDepth) + TEXT("}"));
	TestTrue(TEXT("one over the cap is broken"), Deep.Status == EConfigStatus::Broken);
	SatisDash::Test::SameText(*this, TEXT("bad field"), Deep.BadField, TEXT("json"));
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

namespace
{
	/** The keys of the removed FRM role (R7), in the order the warning names them. */
	const TCHAR* const RemovedKeyNames[] = {
		TEXT("frmPort"), TEXT("frmToken"), TEXT("playersSource"), TEXT("resourceNodesSource"), TEXT("factorySource"), TEXT("trainsSource"),
		TEXT("railsSource"), TEXT("conveyorsSource"), TEXT("mapMarkersSource"), TEXT("droneRoutesSource"), TEXT("powerLinesSource"),
		TEXT("extractorsSource"), TEXT("generatorsSource"), TEXT("stationRatesSource"), TEXT("storageSource"),
	};
	const TCHAR* const ConfigStart = TEXT("{\"backendUrl\":\"https://api.example.com\"");

	int32 CountLinesWith(const TArray<FString>& Lines, const TCHAR* Part)
	{
		int32 N = 0;
		for (const FString& Line : Lines)
		{
			N += Line.Contains(Part) ? 1 : 0;
		}
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigRemovedKeysTest, "SatisDash.Core.Config.RemovedKeys", SatisDash::Test::Flags)

bool FConfigRemovedKeysTest::RunTest(const FString& Parameters)
{
	// R7 (owner decision D2): a key of the removed FRM role never marks the config Broken, whatever its value; it is named, not read.
	const FConfigResult Plain = ParseConfig(FString(ConfigStart) + TEXT("}"));
	TestTrue(TEXT("no removed key: valid"), Plain.Status == EConfigStatus::Valid);
	TestEqual(TEXT("no removed key: nothing ignored"), Plain.IgnoredKeys.Num(), 0);
	TestTrue(TEXT("no removed key: no line"), IgnoredKeysLine(Plain).IsEmpty());

	// Each key alone, with a value that was valid, one that was refused before, and null.
	for (const TCHAR* Key : RemovedKeyNames)
	{
		for (const TCHAR* Value : { TEXT("\"bridge\""), TEXT("\"native\""), TEXT("\"Bad value\""), TEXT("0"), TEXT("65536"), TEXT("true"), TEXT("null"), TEXT("{}") })
		{
			const FConfigResult R = ParseConfig(FString(ConfigStart) + TEXT(",\"") + Key + TEXT("\":") + Value + TEXT("}"));
			TestTrue(*FString::Printf(TEXT("%s %s: valid"), Key, Value), R.Status == EConfigStatus::Valid);
			TestEqual(*FString::Printf(TEXT("%s %s: one key ignored"), Key, Value), R.IgnoredKeys.Num(), 1);
			if (R.IgnoredKeys.Num() == 1)
			{
				SatisDash::Test::SameText(*this, TEXT("and it is named"), R.IgnoredKeys[0], Key);
			}
			const FString Line = IgnoredKeysLine(R);
			TestTrue(*FString::Printf(TEXT("%s %s: the line names the key"), Key, Value), Line.Contains(Key));
			TestFalse(*FString::Printf(TEXT("%s %s: the line has no value"), Key, Value), Line.Contains(TEXT("Bad value")) || Line.Contains(TEXT("65536")));
		}
	}

	// All together: one result, every name once, in the fixed order, and the token's text nowhere.
	FString All = ConfigStart;
	for (const TCHAR* Key : RemovedKeyNames)
	{
		All += FString(TEXT(",\"")) + Key + TEXT("\":") + (FCString::Strcmp(Key, TEXT("frmToken")) == 0 ? TEXT("\"tok-123-secret\"") : TEXT("\"bridge\""));
	}
	All += TEXT("}");
	const FConfigResult Everything = ParseConfig(All);
	TestTrue(TEXT("all keys: valid"), Everything.Status == EConfigStatus::Valid);
	TestEqual(TEXT("all keys: fifteen ignored"), Everything.IgnoredKeys.Num(), 15);
	const FString Line = IgnoredKeysLine(Everything);
	SatisDash::Test::SameText(*this, TEXT("the one line"), Line,
		TEXT("Config: ignored removed keys frmPort, frmToken, playersSource, resourceNodesSource, factorySource, trainsSource, railsSource, conveyorsSource, mapMarkersSource, droneRoutesSource, powerLinesSource, extractorsSource, generatorsSource, stationRatesSource, storageSource (FRM was removed in R7)"));
	TestFalse(TEXT("no token in the line"), Line.Contains(TEXT("tok-123")));

	// Write-back drops them (the code is blanked after enrolment; the file is then rewritten).
	SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.File(TEXT("SatisDashAgent.cfg"));
	TestFalse(TEXT("a config never writes a removed key"), ConfigToJson(Everything.Config).Contains(TEXT("frm")) || ConfigToJson(Everything.Config).Contains(TEXT("Source")));
	SatisDash::Test::WriteText(Path, All);
	TestTrue(TEXT("blanking writes"), BlankConfigCode(Path, LoadConfigFile(Path).Config));
	const FString Rewritten = SatisDash::Test::ReadText(Path);
	TestFalse(TEXT("the rewritten file holds no removed key"), Rewritten.Contains(TEXT("frm")) || Rewritten.Contains(TEXT("Source")) || Rewritten.Contains(TEXT("tok-123")));
	TestEqual(TEXT("and loads without a warning"), LoadConfigFile(Path).IgnoredKeys.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigRemovedKeysLogTest, "SatisDash.Core.Config.RemovedKeysLog", SatisDash::Test::Flags)

bool FConfigRemovedKeysLogTest::RunTest(const FString& Parameters)
{
	// One Display line per load that finds a removed key, names only; none for a file without them.
	const SatisDash::Test::FTempDir Dir;
	const FString ConfigPath = Dir.Path / TEXT("SatisDashAgent.cfg");
	SatisDash::Test::WriteText(ConfigPath, TEXT("{\"backendUrl\":\"https://example.com\",\"frmPort\":18081,\"frmToken\":\"tok-123-secret\",\"factorySource\":\"bridge\"}"));
	{
		SatisDash::Test::FLogCapture Capture;
		FAgent Agent{ConfigPath, Dir.Path / TEXT("credential.json"), [] { return 1.0; }};
		Agent.Boot();
		Agent.CheckConfig(); // the file is unchanged: not loaded again, so no second line
		const TArray<FString> Lines = Capture.Take();
		TestEqual(TEXT("one ignored-keys line"), CountLinesWith(Lines, TEXT("ignored removed keys")), 1);
		TestEqual(TEXT("it names the keys"), CountLinesWith(Lines, TEXT("frmPort, frmToken, factorySource (FRM was removed in R7)")), 1);
		TestTrue(TEXT("the config is not broken"), Agent.GetState() != EAgentState::ConfigBroken);
		for (const FString& Line : Lines)
		{
			TestFalse(TEXT("no token"), Line.Contains(TEXT("tok-123")));
			TestFalse(TEXT("no port"), Line.Contains(TEXT("18081")));
		}
		SatisDash::Test::NoErrorLines(*this, Capture);
	}
	SatisDash::Test::WriteText(ConfigPath, TEXT("{\"backendUrl\":\"https://example.com\"}"));
	{
		SatisDash::Test::FLogCapture Capture;
		FAgent Agent{ConfigPath, Dir.Path / TEXT("credential.json"), [] { return 1.0; }};
		Agent.Boot();
		TestEqual(TEXT("a config without them logs nothing"), CountLinesWith(Capture.Take(), TEXT("ignored removed keys")), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigStepDiagnosticsTest, "SatisDash.Core.Config.StepDiagnostics", SatisDash::Test::Flags)

bool FConfigStepDiagnosticsTest::RunTest(const FString& Parameters)
{
	const FString Base = TEXT("{\"backendUrl\":\"https://api.satis-manager.com\"");
	const FConfigResult None = ParseConfig(Base + TEXT("}"));
	TestTrue(TEXT("no key: valid"), None.Status == EConfigStatus::Valid);

	// "stepDiagnostics" (owner 2026-11-01): a boolean, absent = false, written back only when true, a non-boolean names the key.
	TestFalse(TEXT("no key: stepDiagnostics is off"), None.Config.bStepDiagnostics);
	TestFalse(TEXT("no key: stepDiagnostics is not written back"), ConfigToJson(None.Config).Contains(TEXT("stepDiagnostics")));
	const FConfigResult DiagOn = ParseConfig(Base + TEXT(",\"stepDiagnostics\":true}"));
	TestTrue(TEXT("stepDiagnostics true: valid and on"), DiagOn.Status == EConfigStatus::Valid && DiagOn.Config.bStepDiagnostics);
	TestTrue(TEXT("round trip keeps stepDiagnostics"), ConfigToJson(DiagOn.Config).Contains(TEXT("\"stepDiagnostics\": true")));
	const FConfigResult DiagBack = ParseConfig(ConfigToJson(DiagOn.Config));
	TestTrue(TEXT("and the written file parses back to on"), DiagBack.Status == EConfigStatus::Valid && DiagBack.Config.bStepDiagnostics);
	const FConfigResult DiagOff = ParseConfig(Base + TEXT(",\"stepDiagnostics\":false}"));
	TestTrue(TEXT("stepDiagnostics false: valid and off"), DiagOff.Status == EConfigStatus::Valid && !DiagOff.Config.bStepDiagnostics);
	for (const TCHAR* Bad : { TEXT("\"true\""), TEXT("1"), TEXT("null"), TEXT("{}") })
	{
		const FConfigResult R = ParseConfig(Base + TEXT(",\"stepDiagnostics\":") + Bad + TEXT("}"));
		TestTrue(*FString::Printf(TEXT("stepDiagnostics %s is refused"), Bad), R.Status == EConfigStatus::Broken);
		TestEqual(*FString::Printf(TEXT("stepDiagnostics %s names the key"), Bad), R.BadField, FString(TEXT("stepDiagnostics")));
	}
	return true;
}
