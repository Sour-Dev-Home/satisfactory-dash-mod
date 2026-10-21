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

		void writeConfigAt(const TCHAR* Url, const TCHAR* Code) const
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
			SatisDash::Test::WriteText(ConfigPath, FString::Printf(TEXT("{\"backendUrl\":\"%s\",\"enrollmentCode\":\"%s\"}"), Url, Code));
		}

		void writeConfig(const TCHAR* Code) const { writeConfigAt(TEXT("https://example.com"), Code); }

		void writeCredential() const
		{
			FCredential Saved;
			Saved.AgentSecret = SatisDash::Test::FakeSecret();
			Saved.ServerId = TEXT("srv-1");
			Saved.BackendUrl = TEXT("https://example.com");
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
		F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
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
	F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
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
	F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
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
	F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
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
	F.writeConfig(TEXT("QQQQ-2222-2222"));
	IFileManager::Get().SetTimeStamp(*F.ConfigPath, Kept);
	Agent.CheckConfig();
	TestTrue(TEXT("new text retries even with the same time"), Agent.GetState() == EAgentState::Enrolling);
	SatisDash::Test::SameText(*this, TEXT("with the new code"), Agent.GetConfig().EnrollmentCode, TEXT("QQQQ-2222-2222"));

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
	F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentEditDuringEnrolTest, "SatisDash.Core.Agent.EditDuringEnrol", SatisDash::Test::Flags)

bool FAgentEditDuringEnrolTest::RunTest(const FString& Parameters)
{
	{
		// A broken edit made while the enrol is in flight is never overwritten from memory.
		const FFixture F;
		F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		Agent.StartEnroll(0);
		SatisDash::Test::WriteText(F.ConfigPath, TEXT("{ broken"));
		Agent.FinishEnroll(SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()), 0);
		SatisDash::Test::SameText(*this, TEXT("the broken file is left as it is"), SatisDash::Test::ReadText(F.ConfigPath), TEXT("{ broken"));
		const FCredentialStore Store{F.CredentialPath};
		TestTrue(TEXT("the credential is saved"), Store.Load().IsSet());
		Agent.CheckConfig();
		TestTrue(TEXT("the next check sees the broken file"), Agent.GetState() == EAgentState::ConfigBroken);
		TestTrue(TEXT("and nothing is handed out"), Agent.GetCredential() == nullptr);
	}
	{
		// A valid edit (a new URL, the same code) keeps its URL; only the code is blanked. The credential was issued by
		// the URL the request went to, so it is not this config's: idle, nothing handed out.
		const FFixture F;
		F.writeConfig(TEXT("AB3D-7XQ2-K5MN"));
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		Agent.StartEnroll(0);
		SatisDash::Test::WriteText(F.ConfigPath, TEXT("{\"backendUrl\":\"https://other.example.com\",\"enrollmentCode\":\"AB3D-7XQ2-K5MN\"}"));
		Agent.FinishEnroll(SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()), 0);
		const FConfigResult Written = ParseConfig(SatisDash::Test::ReadText(F.ConfigPath));
		SatisDash::Test::SameText(*this, TEXT("the new URL is kept"), Written.Config.BackendUrl, TEXT("https://other.example.com"));
		TestTrue(TEXT("the code is blanked"), Written.Config.EnrollmentCode.IsEmpty());
		TestTrue(TEXT("idle: the credential belongs to the old URL"), Agent.GetState() == EAgentState::Idle);
		TestTrue(TEXT("nothing is handed out"), Agent.GetCredential() == nullptr);
		const FCredentialStore Store{F.CredentialPath};
		const TOptional<FCredential> Saved = Store.Load();
		TestTrue(TEXT("the credential is saved for the URL that issued it"), Saved.IsSet() && Saved->BackendUrl == TEXT("https://example.com"));
		F.writeConfig(TEXT(""));
		Agent.CheckConfig();
		TestTrue(TEXT("back at the issuing URL: running"), Agent.GetState() == EAgentState::Running);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgentBackendBindingTest, "SatisDash.Core.Agent.BackendBinding", SatisDash::Test::Flags)

bool FAgentBackendBindingTest::RunTest(const FString& Parameters)
{
	{
		// A new URL with a fresh code enrols there, and the new credential carries the new URL.
		const FFixture F;
		F.writeCredential();
		F.writeConfigAt(TEXT("https://other.example.com"), TEXT("AB3D-7XQ2-K5MN"));
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		TestTrue(TEXT("a credential for another backend: enrolling"), Agent.GetState() == EAgentState::Enrolling);
		TestTrue(TEXT("the old secret is not handed out"), Agent.GetCredential() == nullptr);
		const TOptional<FRequestSpec> Request = Agent.StartEnroll(0);
		TestTrue(TEXT("enrols at the new URL"), Request.IsSet() && Request->Url.StartsWith(TEXT("https://other.example.com/")));
		Agent.FinishEnroll(SatisDash::Test::Reply(404, TEXT("{}")), 0);
		const FCredentialStore Store{F.CredentialPath};
		TestTrue(TEXT("a failed enrol leaves the old file"), Store.Load().IsSet() && Store.Load()->BackendUrl == TEXT("https://example.com"));
		F.writeConfigAt(TEXT("https://other.example.com"), TEXT("QQQQ-2222-2222"));
		Agent.CheckConfig();
		Agent.StartEnroll(0);
		Agent.FinishEnroll(SatisDash::Test::Reply(201, SatisDash::Test::EnrollJson()), 0);
		TestTrue(TEXT("running"), Agent.GetState() == EAgentState::Running);
		const FCredential* Handed = Agent.GetCredential();
		TestTrue(TEXT("the new credential carries the new URL"), Handed != nullptr && Handed->BackendUrl == TEXT("https://other.example.com"));
		TestTrue(TEXT("and so does the file"), Store.Load().IsSet() && Store.Load()->BackendUrl == TEXT("https://other.example.com"));
	}
	{
		// A new URL without a code: idle with one line, never a Bearer; the original URL resumes.
		const FFixture F;
		F.writeCredential();
		F.writeConfigAt(TEXT("https://other.example.com"), TEXT(""));
		SatisDash::Test::FLogCapture Capture;
		FAgent Agent = F.makeAgent();
		Agent.Boot();
		Agent.CheckConfig();
		TestTrue(TEXT("idle"), Agent.GetState() == EAgentState::Idle);
		TestTrue(TEXT("no credential handed out"), Agent.GetCredential() == nullptr);
		TestFalse(TEXT("no enrol request"), Agent.StartEnroll(0).IsSet());
		int32 Lines = 0;
		for (const FString& Line : Capture.Take())
		{
			Lines += Line.Contains(TEXT("different backend")) ? 1 : 0;
		}
		TestEqual(TEXT("one line about the different backend"), Lines, 1);
		const FCredentialStore Store{F.CredentialPath};
		TestTrue(TEXT("the old credential file is untouched"), Store.Load().IsSet());

		F.writeConfig(TEXT(""));
		Agent.CheckConfig();
		TestTrue(TEXT("switching back resumes running"), Agent.GetState() == EAgentState::Running);
		TestTrue(TEXT("with the old credential"), Agent.GetCredential() != nullptr);
	}
	return true;
}
