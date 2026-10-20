#include "SatisDash/Log.h"
#include "SatisDash/Version.h"
#include "TestUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSatisDashVersionTest, "SatisDash.Core.Version", SatisDash::Test::Flags)

bool FSatisDashVersionTest::RunTest(const FString& Parameters)
{
	const FString Version = SatisDash::AgentVersion();
	SatisDash::Test::SameText(*this, TEXT("agentVersion"), Version, TEXT("mod/0.1.0"));
	TestTrue(TEXT("at most 32 characters"), Version.Len() <= 32);
	return true;
}

// Probes how this project's test settings treat a Warning logged during a test. The mod logs failures at Warning, so
// the judge must still count such a test as passed (succeededWithWarnings).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSatisDashWarningProbeTest, "SatisDash.Core.WarningProbe", SatisDash::Test::Flags)

bool FSatisDashWarningProbeTest::RunTest(const FString& Parameters)
{
	UE_LOG(LogSatisDash, Warning, TEXT("warning probe (expected in the test run)"));
	return true;
}
