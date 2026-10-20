#include "HAL/FileManager.h"
#include "SatisDash/Agent.h"
#include "SatisDash/Config.h"
#include "TestUtil.h"

using namespace SatisDash;

namespace
{
	struct FFixture
	{
		SatisDash::Test::FTempDir Dir;
		FString ConfigPath = Dir.Path / TEXT("Configs/SatisDashAgent.cfg");
		FString CredentialPath = Dir.Path / TEXT("SatisDash/credential.json");

		void writeConfig(const TCHAR* Code) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			SatisDash::Test::WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"https://example.com\",\"enrollmentCode\":\"%s\"}"), Code));
		}

		void writeCredential() const
		{
			FCredential Saved;
			Saved.AgentSecret = SatisDash::Test::FakeSecret();
			Saved.ServerId = TEXT("srv-1");
			FCredentialStore Store{CredentialPath};
			Store.Save(Saved);
		}

		FAgent makeAgent() const { return FAgent(ConfigPath, CredentialPath, [] { return 1.0; }); }
		FString codeInFile() const { return ParseConfig(SatisDash::Test::ReadText(ConfigPath)).Config.EnrollmentCode; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentBootTest, "SatisDash.Core.Agent.Boot", SatisDash::Test::Flags)

bool FAgentBootTest::RunTest(const FString& Parameters)
{
	{
		const FFixture F;
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		TestTrue(TEXT("first boot: idle"), Agent.GetState() == EAgentState::Idle);
		TestTrue(TEXT("and the config is written"), IFileManager::Get().FileExists(*F.ConfigPath));
	}
	{
		// The crash case: the credential was written but the code not yet blanked. The credential wins.
		const FFixture F;
		F.writeConfig(TEXT("AB3D-7XQ2"));
		F.writeCredential();
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		TestTrue(TEXT("credential wins: running"), Agent.GetState() == EAgentState::Running);
		TestTrue(TEXT("the code is blanked then"), F.codeInFile().IsEmpty());
		TestTrue(TEXT("never enrols with it"), !Agent.StartEnroll(0).IsSet());
	}
	{
		const FFixture F;
		F.writeConfig(TEXT(""));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(F.CredentialPath), true);
		SatisDash::Test::WriteText(F.CredentialPath, TEXT("{ broken"));
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		TestTrue(TEXT("a broken credential is not enrolled"), Agent.GetState() == EAgentState::Idle);
		TestTrue(TEXT("and never a Bearer"), Agent.GetCredential() == nullptr);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentEnrolTest, "SatisDash.Core.Agent.Enrol", SatisDash::Test::Flags)

bool FAgentEnrolTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.writeConfig(TEXT("AB3D-7XQ2"));
	FAgent Agent = F.makeAgent();
	Agent.Boot();
	TestTrue(TEXT("enrolling"), Agent.GetState() == EAgentState::Enrolling);
	const TOptional<FRequestSpec> First = Agent.StartEnroll(0);
	TestTrue(TEXT("an enrol request"), First.IsSet() && First->Url.EndsWith(TEXT("/agent/v1/enroll")));
	TestFalse(TEXT("one at a time"), Agent.StartEnroll(0).IsSet());

	Agent.FinishEnroll(SatisDash::Test::Reply(503, TEXT("{}")), 0);
	TestTrue(TEXT("still enrolling after a 503"), Agent.GetState() == EAgentState::Enrolling);
	TestFalse(TEXT("waits the backoff"), Agent.StartEnroll(0.5).IsSet());
	TestTrue(TEXT("then retries"), Agent.StartEnroll(1.0).IsSet());

	Agent.FinishEnroll(SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()), 1.0);
	TestTrue(TEXT("running"), Agent.GetState() == EAgentState::Running);
	const FCredentialStore Store{F.CredentialPath};
	TestTrue(TEXT("credential saved"), Store.Load().IsSet());
	TestTrue(TEXT("code blanked"), F.codeInFile().IsEmpty());
	TestTrue(TEXT("credential in memory"), Agent.GetCredential() != nullptr);
	TestEqual(TEXT("cadence from the answer"), Agent.GetCadence().StatusSeconds, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentRejectedTest, "SatisDash.Core.Agent.CodeRejected", SatisDash::Test::Flags)

bool FAgentRejectedTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.writeConfig(TEXT("AB3D-7XQ2"));
	FAgent Agent = F.makeAgent();
	Agent.Boot();
	Agent.StartEnroll(0);
	Agent.FinishEnroll(SatisDash::Test::Reply(404, TEXT("{}")), 0);
	TestTrue(TEXT("rejected"), Agent.GetState() == EAgentState::CodeRejected);
	TestFalse(TEXT("the code is kept"), F.codeInFile().IsEmpty());
	TestFalse(TEXT("and not retried"), Agent.StartEnroll(1000).IsSet());
	Agent.CheckConfig();
	TestTrue(TEXT("an unchanged config doesn't retry"), Agent.GetState() == EAgentState::CodeRejected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentConfigChangeTest, "SatisDash.Core.Agent.ConfigChange", SatisDash::Test::Flags)

bool FAgentConfigChangeTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.writeConfig(TEXT("AB3D-7XQ2"));
	FAgent Agent = F.makeAgent();
	Agent.Boot();
	Agent.StartEnroll(0);
	Agent.FinishEnroll(SatisDash::Test::Reply(400, TEXT("{}")), 0);

	// The same text, saved again: only the modification time moves.
	IFileManager::Get().SetTimeStamp(*F.ConfigPath, ConfigModTime(F.ConfigPath) + FTimespan::FromMinutes(1));
	Agent.CheckConfig();
	TestTrue(TEXT("a new modification time retries"), Agent.GetState() == EAgentState::Enrolling);
	Agent.StartEnroll(0);
	Agent.FinishEnroll(SatisDash::Test::Reply(400, TEXT("{}")), 0);

	// A new code with the old modification time (an upload that keeps the timestamp).
	const FDateTime Kept = ConfigModTime(F.ConfigPath);
	F.writeConfig(TEXT("QQQQ-2222"));
	IFileManager::Get().SetTimeStamp(*F.ConfigPath, Kept);
	Agent.CheckConfig();
	TestTrue(TEXT("new text retries even with the same time"), Agent.GetState() == EAgentState::Enrolling);
	SatisDash::Test::SameText(*this, TEXT("with the new code"), Agent.GetConfig().EnrollmentCode, TEXT("QQQQ-2222"));

	SatisDash::Test::WriteText(F.ConfigPath, TEXT("{ broken"));
	Agent.CheckConfig();
	TestTrue(TEXT("broken"), Agent.GetState() == EAgentState::ConfigBroken);
	F.writeConfig(TEXT(""));
	Agent.CheckConfig();
	TestTrue(TEXT("fixed, no code: idle"), Agent.GetState() == EAgentState::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentHaltTest, "SatisDash.Core.Agent.Halt", SatisDash::Test::Flags)

bool FAgentHaltTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	F.writeConfig(TEXT(""));
	F.writeCredential();
	FAgent Agent = F.makeAgent();
	Agent.Boot();
	Agent.OnAuthRejected();
	TestTrue(TEXT("halted"), Agent.GetState() == EAgentState::Halted);
	TestTrue(TEXT("no credential to send"), Agent.GetCredential() == nullptr);
	TestFalse(TEXT("the file is deleted"), IFileManager::Get().FileExists(*F.CredentialPath));
	Agent.CheckConfig();
	TestTrue(TEXT("stays halted"), Agent.GetState() == EAgentState::Halted);
	F.writeConfig(TEXT("AB3D-7XQ2"));
	Agent.CheckConfig();
	TestTrue(TEXT("a new code re-enrols"), Agent.GetState() == EAgentState::Enrolling);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentFailClosedTest, "SatisDash.Core.Agent.FailClosed", SatisDash::Test::Flags)

bool FAgentFailClosedTest::RunTest(const FString& Parameters)
{
	const FFixture F;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(F.ConfigPath), true);
	SatisDash::Test::WriteText(F.ConfigPath, TEXT("{\"backendUrl\":\"http://example.com\"}"));
	F.writeCredential();
	FAgent Agent = F.makeAgent();
	Agent.Boot();
	TestTrue(TEXT("a broken config with a credential: broken"), Agent.GetState() == EAgentState::ConfigBroken);
	TestTrue(TEXT("and nothing is sent: no credential handed out"), Agent.GetCredential() == nullptr);
	SatisDash::Test::SameText(*this, TEXT("the broken file is left as it is"), SatisDash::Test::ReadText(F.ConfigPath), TEXT("{\"backendUrl\":\"http://example.com\"}"));
	return true;
}
