#include "HAL/FileManager.h"
#include "SatisDash/Credential.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	// A fake secret of the contract's shape (43 base64url characters). Never a real credential (spec, spikes rule).
	const FString FakeSecret = FString::ChrN(42, TEXT('a')) + TEXT("_");

	FCredential Fake()
	{
		FCredential Credential;
		Credential.AgentSecret = FakeSecret;
		Credential.ServerId = TEXT("srv-1");
		return Credential;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCredentialFieldsTest, "SatisDash.Core.Credential.Fields", SatisDash::Test::Flags)

bool FCredentialFieldsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("43 base64url characters"), IsAgentSecret(FakeSecret));
	TestFalse(TEXT("empty"), IsAgentSecret(TEXT("")));
	TestFalse(TEXT("a space"), IsAgentSecret(TEXT("abc def")));
	TestFalse(TEXT("CR LF (a header injection)"), IsAgentSecret(TEXT("abc\r\nX-Evil: 1")));
	TestFalse(TEXT("base64 '+', '/', '='"), IsAgentSecret(TEXT("ab+/=")));
	TestFalse(TEXT("over 256 characters"), IsAgentSecret(FString::ChrN(257, TEXT('a'))));
	TestTrue(TEXT("a server id"), IsServerId(TEXT("srv-1")));
	TestFalse(TEXT("upper case"), IsServerId(TEXT("SRV")));
	TestFalse(TEXT("33 characters"), IsServerId(FString::ChrN(33, TEXT('a'))));
	TestFalse(TEXT("empty id"), IsServerId(TEXT("")));

	const TOptional<FCredential> Parsed = ParseCredential(CredentialToJson(Fake()));
	TestTrue(TEXT("round trip"), Parsed.IsSet());
	if (Parsed.IsSet())
	{
		SatisDash::Test::SameText(*this, TEXT("secret"), Parsed->AgentSecret, FakeSecret);
		SatisDash::Test::SameText(*this, TEXT("server id"), Parsed->ServerId, TEXT("srv-1"));
	}
	TestFalse(TEXT("wrong type"), ParseCredential(TEXT("{\"agentSecret\":5,\"serverId\":\"srv-1\"}")).IsSet());
	TestFalse(TEXT("missing id"), ParseCredential(FString::Printf(TEXT("{\"agentSecret\":\"%s\"}"), *FakeSecret)).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCredentialStoreTest, "SatisDash.Core.Credential.Store", SatisDash::Test::Flags)

bool FCredentialStoreTest::RunTest(const FString& Parameters)
{
	const SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.Path / TEXT("SatisDash/credential.json");
	const FCredentialStore Store{Path};

	TestFalse(TEXT("nothing saved yet"), Store.Load().IsSet());
	TestFalse(TEXT("no file yet"), Store.Exists());
	TestTrue(TEXT("save creates the folder"), Store.Save(Fake()));
	TestTrue(TEXT("load after save"), Store.Load().IsSet() && Store.Load()->AgentSecret.Equals(FakeSecret, ESearchCase::CaseSensitive));

	SatisDash::Test::WriteText(Path, TEXT("{ not json"));
	TestFalse(TEXT("a broken file loads as nothing"), Store.Load().IsSet());
	TestTrue(TEXT("but it exists"), Store.Exists());

	TestTrue(TEXT("delete"), Store.Delete());
	TestFalse(TEXT("gone"), Store.Exists());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCredentialCrashTest, "SatisDash.Core.Credential.Crash", SatisDash::Test::Flags)

bool FCredentialCrashTest::RunTest(const FString& Parameters)
{
	const SatisDash::Test::FTempDir Dir;
	const FString Path = Dir.Path / TEXT("credential.json");
	const FString Temp = Path + TEXT(".tmp");
	const FCredentialStore Store{Path};

	// A crash mid-write: the old file is whole and a half-written temp file is left beside it.
	Store.Save(Fake());
	SatisDash::Test::WriteText(Temp, TEXT("{\"agentSec"));
	TestTrue(TEXT("the whole file wins over a broken temp"), Store.Load().IsSet());

	// A crash between a replace's delete and its move: only a valid temp file is left.
	IFileManager::Get().Delete(*Path);
	SatisDash::Test::WriteText(Temp, CredentialToJson(Fake()));
	TestTrue(TEXT("a valid temp file is recovered"), Store.Load().IsSet());
	TestTrue(TEXT("and moved into place"), IFileManager::Get().FileExists(*Path));

	// Only a broken temp file: not enrolled.
	IFileManager::Get().Delete(*Path);
	SatisDash::Test::WriteText(Temp, TEXT("{\"agentSec"));
	TestFalse(TEXT("a broken temp file alone is nothing"), Store.Load().IsSet());

	TestTrue(TEXT("delete clears both"), Store.Delete() && !IFileManager::Get().FileExists(*Temp));
	return true;
}
