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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigFrmTest, "SatisDash.Core.Config.Frm", SatisDash::Test::Flags)

bool FConfigFrmTest::RunTest(const FString& Parameters)
{
	const FConfigResult Plain = ParseConfig(TEXT("{\"backendUrl\":\"https://api.example.com\"}"));
	TestTrue(TEXT("valid without the FRM fields"), Plain.Status == EConfigStatus::Valid);
	TestEqual(TEXT("FRM's default port"), Plain.Config.GetFrmPort(), 8080);
	TestTrue(TEXT("no token"), Plain.Config.FrmToken.IsEmpty());

	const FConfigResult Set = ParseConfig(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmPort\":18081,\"frmToken\":\"  tok-123  \"}"));
	TestTrue(TEXT("valid with both"), Set.Status == EConfigStatus::Valid);
	TestEqual(TEXT("the port"), Set.Config.GetFrmPort(), 18081);
	SatisDash::Test::SameText(*this, TEXT("the token, trimmed"), Set.Config.FrmToken, TEXT("tok-123"));

	for (const TCHAR* Bad : { TEXT("0"), TEXT("65536"), TEXT("80.5"), TEXT("\"8080\""), TEXT("true"), TEXT("null"), TEXT("-1") })
	{
		const FConfigResult R = ParseConfig(FString::Printf(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmPort\":%s}"), Bad));
		TestTrue(FString::Printf(TEXT("frmPort %s is broken"), Bad), R.Status == EConfigStatus::Broken);
		SatisDash::Test::SameText(*this, TEXT("and names frmPort"), R.BadField, TEXT("frmPort"));
	}
	for (const TCHAR* Bad : { TEXT("\"a\\nb\""), TEXT("\"a\\rb\""), TEXT("\"a\\u0007b\""), TEXT("42") })
	{
		const FConfigResult R = ParseConfig(FString::Printf(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmToken\":%s}"), Bad));
		SatisDash::Test::SameText(*this, TEXT("a bad token names frmToken"), R.BadField, TEXT("frmToken"));
	}
	TestTrue(TEXT("a trailing newline is trimmed, not refused"),
		ParseConfig(TEXT("{\"backendUrl\":\"https://api.example.com\",\"frmToken\":\"tok\\n\"}")).Status == EConfigStatus::Valid);

	SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.File(TEXT("SatisDashAgent.cfg"));
	FAgentConfig Config = Set.Config;
	Config.EnrollmentCode = TEXT("AB3D-7XQ2-K5MN");
	TestTrue(TEXT("blanking writes"), BlankConfigCode(Path, Config));
	const FConfigResult Back = LoadConfigFile(Path);
	TestEqual(TEXT("blanking keeps the port"), Back.Config.GetFrmPort(), 18081);
	SatisDash::Test::SameText(*this, TEXT("and the token"), Back.Config.FrmToken, TEXT("tok-123"));
	TestTrue(TEXT("and blanks the code"), Back.Config.EnrollmentCode.IsEmpty());
	TestFalse(TEXT("a default port isn't written"), ConfigToJson(Plain.Config).Contains(TEXT("frmPort")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConfigSourcesTest, "SatisDash.Core.Config.Sources", SatisDash::Test::Flags)

bool FConfigSourcesTest::RunTest(const FString& Parameters)
{
	const FString Base = TEXT("{\"backendUrl\":\"https://api.satis-manager.com\"");
	const FConfigResult None = ParseConfig(Base + TEXT("}"));
	TestTrue(TEXT("no key: valid"), None.Status == EConfigStatus::Valid);
	TestTrue(TEXT("no key: players default"), None.Config.GetPlayersSource() == DefaultPlayersSource);
	TestTrue(TEXT("no key: resource nodes default"), None.Config.GetResourceNodesSource() == DefaultResourceNodesSource);
	TestFalse(TEXT("no key: nothing set"), None.Config.PlayersSource.IsSet() || None.Config.ResourceNodesSource.IsSet());

	const FConfigResult Native = ParseConfig(Base + TEXT(",\"playersSource\":\"native\",\"resourceNodesSource\":\"bridge\"}"));
	TestTrue(TEXT("both keys: valid"), Native.Status == EConfigStatus::Valid);
	TestTrue(TEXT("players native"), Native.Config.GetPlayersSource() == ESource::Native);
	TestTrue(TEXT("resource nodes bridge"), Native.Config.GetResourceNodesSource() == ESource::Bridge);

	for (const TCHAR* Bad : { TEXT("\"Native\""), TEXT("\"native \""), TEXT("\"\""), TEXT("1"), TEXT("true"), TEXT("null") })
	{
		const FConfigResult R = ParseConfig(Base + TEXT(",\"playersSource\":") + Bad + TEXT("}"));
		TestTrue(*FString::Printf(TEXT("%s is refused"), Bad), R.Status == EConfigStatus::Broken);
		TestEqual(*FString::Printf(TEXT("%s names the key"), Bad), R.BadField, FString(TEXT("playersSource")));
	}
	TestEqual(TEXT("the other key is named too"), ParseConfig(Base + TEXT(",\"resourceNodesSource\":\"x\"}")).BadField, FString(TEXT("resourceNodesSource")));

	// The factory and trains keys: default native (the S2 switch), the same strict parse, and they name themselves.
	TestTrue(TEXT("no key: factory default is native"), None.Config.GetFactorySource() == ESource::Native && DefaultFactorySource == ESource::Native);
	TestTrue(TEXT("no key: trains default is native"), None.Config.GetTrainsSource() == ESource::Native && DefaultTrainsSource == ESource::Native);
	TestFalse(TEXT("no key: factory and trains unset"), None.Config.FactorySource.IsSet() || None.Config.TrainsSource.IsSet());
	const FConfigResult Parts = ParseConfig(Base + TEXT(",\"factorySource\":\"bridge\",\"trainsSource\":\"bridge\"}"));
	TestTrue(TEXT("part keys: valid"), Parts.Status == EConfigStatus::Valid);
	TestTrue(TEXT("the factory key still selects the bridge"), Parts.Config.GetFactorySource() == ESource::Bridge);
	TestTrue(TEXT("the trains key still selects the bridge"), Parts.Config.GetTrainsSource() == ESource::Bridge);
	for (const TCHAR* Key : { TEXT("factorySource"), TEXT("trainsSource") })
	{
		for (const TCHAR* Bad : { TEXT("\"Native\""), TEXT("\"\""), TEXT("1"), TEXT("null") })
		{
			const FConfigResult R = ParseConfig(Base + TEXT(",\"") + Key + TEXT("\":") + Bad + TEXT("}"));
			TestTrue(*FString::Printf(TEXT("%s %s is refused"), Key, Bad), R.Status == EConfigStatus::Broken);
			TestEqual(*FString::Printf(TEXT("%s %s names the key"), Key, Bad), R.BadField, FString(Key));
		}
	}
	const FString PartsJson = ConfigToJson(Parts.Config);
	TestTrue(TEXT("round trip keeps the part keys"), PartsJson.Contains(TEXT("\"factorySource\": \"bridge\"")) && PartsJson.Contains(TEXT("\"trainsSource\": \"bridge\"")));

	// The rails key: default native (the S3 switch), the same strict parse, names itself, round-trips only when set.
	TestTrue(TEXT("no key: rails default is native"), None.Config.GetRailsSource() == ESource::Native && DefaultRailsSource == ESource::Native);
	TestFalse(TEXT("no key: rails unset"), None.Config.RailsSource.IsSet());
	const FConfigResult RailsBridge = ParseConfig(Base + TEXT(",\"railsSource\":\"bridge\"}"));
	TestTrue(TEXT("rails key: valid"), RailsBridge.Status == EConfigStatus::Valid);
	TestTrue(TEXT("the rails key still selects the bridge"), RailsBridge.Config.GetRailsSource() == ESource::Bridge);
	TestTrue(TEXT("round trip keeps the rails key"), ConfigToJson(RailsBridge.Config).Contains(TEXT("\"railsSource\": \"bridge\"")));
	for (const TCHAR* Bad : { TEXT("\"Native\""), TEXT("\"\""), TEXT("1"), TEXT("null") })
	{
		const FConfigResult R = ParseConfig(Base + TEXT(",\"railsSource\":") + Bad + TEXT("}"));
		TestTrue(*FString::Printf(TEXT("railsSource %s is refused"), Bad), R.Status == EConfigStatus::Broken);
		TestEqual(*FString::Printf(TEXT("railsSource %s names the key"), Bad), R.BadField, FString(TEXT("railsSource")));
	}

	// The conveyors key: the same shape (default native, the S3 switch; bridge means the layer is not sent).
	TestTrue(TEXT("no key: conveyors default is native"), None.Config.GetConveyorsSource() == ESource::Native && DefaultConveyorsSource == ESource::Native);
	TestFalse(TEXT("no key: conveyors unset"), None.Config.ConveyorsSource.IsSet());
	const FConfigResult ConveyorsBridge = ParseConfig(Base + TEXT(",\"conveyorsSource\":\"bridge\"}"));
	TestTrue(TEXT("conveyors key: valid"), ConveyorsBridge.Status == EConfigStatus::Valid);
	TestTrue(TEXT("the conveyors key still selects the bridge"), ConveyorsBridge.Config.GetConveyorsSource() == ESource::Bridge);
	TestTrue(TEXT("and leaves rails on its default"), ConveyorsBridge.Config.GetRailsSource() == ESource::Native);
	TestTrue(TEXT("round trip keeps the conveyors key"), ConfigToJson(ConveyorsBridge.Config).Contains(TEXT("\"conveyorsSource\": \"bridge\"")));
	for (const TCHAR* Bad : { TEXT("\"Native\""), TEXT("\"\""), TEXT("1"), TEXT("null") })
	{
		const FConfigResult R = ParseConfig(Base + TEXT(",\"conveyorsSource\":") + Bad + TEXT("}"));
		TestTrue(*FString::Printf(TEXT("conveyorsSource %s is refused"), Bad), R.Status == EConfigStatus::Broken);
		TestEqual(*FString::Printf(TEXT("conveyorsSource %s names the key"), Bad), R.BadField, FString(TEXT("conveyorsSource")));
	}

	// The S4-A keys: default bridge (the layer is not sent), the same strict parse, each names itself, round-trips only when set.
	const struct { const TCHAR* Key; ESource Default; ESource (FAgentConfig::*Getter)() const; TOptional<ESource> FAgentConfig::*Field; } S4Keys[] = {
		{ TEXT("mapMarkersSource"), DefaultMapMarkersSource, &FAgentConfig::GetMapMarkersSource, &FAgentConfig::MapMarkersSource },
		{ TEXT("droneRoutesSource"), DefaultDroneRoutesSource, &FAgentConfig::GetDroneRoutesSource, &FAgentConfig::DroneRoutesSource },
		{ TEXT("powerLinesSource"), DefaultPowerLinesSource, &FAgentConfig::GetPowerLinesSource, &FAgentConfig::PowerLinesSource },
	};
	for (const auto& K : S4Keys)
	{
		TestTrue(*FString::Printf(TEXT("no key: %s default is bridge"), K.Key), (None.Config.*K.Getter)() == ESource::Bridge && K.Default == ESource::Bridge);
		TestFalse(*FString::Printf(TEXT("no key: %s unset"), K.Key), (None.Config.*K.Field).IsSet());
		const FConfigResult Nat = ParseConfig(Base + TEXT(",\"") + K.Key + TEXT("\":\"native\"}"));
		TestTrue(*FString::Printf(TEXT("%s native: valid"), K.Key), Nat.Status == EConfigStatus::Valid);
		TestTrue(*FString::Printf(TEXT("%s native selects native"), K.Key), (Nat.Config.*K.Getter)() == ESource::Native);
		TestTrue(*FString::Printf(TEXT("round trip keeps %s"), K.Key), ConfigToJson(Nat.Config).Contains(FString::Printf(TEXT("\"%s\": \"native\""), K.Key)));
		for (const TCHAR* Bad : { TEXT("\"Native\""), TEXT("\"\""), TEXT("1"), TEXT("null") })
		{
			const FConfigResult R = ParseConfig(Base + TEXT(",\"") + K.Key + TEXT("\":") + Bad + TEXT("}"));
			TestTrue(*FString::Printf(TEXT("%s %s is refused"), K.Key, Bad), R.Status == EConfigStatus::Broken);
			TestEqual(*FString::Printf(TEXT("%s %s names the key"), K.Key, Bad), R.BadField, FString(K.Key));
		}
	}

	// Written back only when set, and kept when the code is blanked after enrolment.
	const FString Json = ConfigToJson(Native.Config);
	TestTrue(TEXT("round trip keeps the keys"), Json.Contains(TEXT("\"playersSource\": \"native\"")) && Json.Contains(TEXT("\"resourceNodesSource\": \"bridge\"")));
	TestFalse(TEXT("unset keys are not written"), ConfigToJson(None.Config).Contains(TEXT("Source")));
	return true;
}
