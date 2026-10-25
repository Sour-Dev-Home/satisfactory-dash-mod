#include "SatisDash/Frm.h"

SatisDash::FRequestSpec SatisDash::BuildFrmRequest(int32 Port, const FString& Token, const FFrmEndpoint& Endpoint)
{
	FRequestSpec Spec;
	Spec.Verb = TEXT("GET");
	Spec.Url = FString::Printf(TEXT("http://127.0.0.1:%d/%s"), Port, Endpoint.Name);
	if (!Token.IsEmpty())
	{
		Spec.Headers.Add({ TEXT("X-FRM-Authorization"), Token });
	}
	Spec.TimeoutSeconds = Endpoint.TimeoutSeconds;
	Spec.ResponseCapBytes = Endpoint.CapBytes;
	return Spec;
}
