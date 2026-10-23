#include "CurlTransport.h"

#include "HAL/PlatformTime.h"
#include "Interfaces/ISslCertificateManager.h"
#include "SatisDash/Config.h"
#include "SatisDash/Log.h"
#include "SatisDashCurl.h"
#include "SslModule.h"

// The engine wires certificates only with WITH_SSL (CurlHttp.cpp:181-186), which SSL.Build.cs:42 sets on Win64 and Unix.
// Without it there would be no trusted roots at all, so such a build doesn't compile rather than send unverified.
#if !WITH_SSL
#error "SatisDashGame's transport needs WITH_SSL: it takes its trusted certificates from the SSL module"
#endif

using SatisDash::FCurlTransport;

namespace
{
	/** scheme://host:port of a URL: everything before the path. */
	FString originOf(const FString& Url)
	{
		const int32 HostAt = Url.Find(TEXT("://"), ESearchCase::CaseSensitive);
		const int32 PathAt = Url.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HostAt == INDEX_NONE ? 0 : HostAt + 3);
		return PathAt == INDEX_NONE ? Url : Url.Left(PathAt);
	}

	FString fromUtf8(const char* Text, int32 Bytes)
	{
		const FUTF8ToTCHAR Converted = FUTF8ToTCHAR(Text, Bytes);
		return FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
	}

	bool hasLineBreak(const FString& Text)
	{
		for (const TCHAR Char : Text)
		{
			if (Char == TEXT('\r') || Char == TEXT('\n'))
			{
				return true;
			}
		}
		return false;
	}

	/** ASCII, case-insensitive: header names are case-insensitive in HTTP. */
	bool startsWithNoCase(const char* Data, size_t Bytes, const char* Prefix)
	{
		size_t At = 0;
		for (; Prefix[At] != '\0'; ++At)
		{
			if (At >= Bytes)
			{
				return false;
			}
			const char Lower = (Data[At] >= 'A' && Data[At] <= 'Z') ? char(Data[At] - 'A' + 'a') : Data[At];
			if (Lower != Prefix[At])
			{
				return false;
			}
		}
		return true;
	}

	/** A short fixed word for the log line (S9): never curl's own text, never any of the URL's path or query. */
	const TCHAR* failureWord(CURLcode Result)
	{
		switch (Result)
		{
		case CURLE_OK:
			return TEXT("None");
		case CURLE_OPERATION_TIMEDOUT:
			return TEXT("TimedOut");
		case CURLE_COULDNT_RESOLVE_PROXY:
		case CURLE_COULDNT_RESOLVE_HOST:
		case CURLE_COULDNT_CONNECT:
		case CURLE_SEND_ERROR:
		case CURLE_RECV_ERROR:
		case CURLE_GOT_NOTHING:
		case CURLE_PARTIAL_FILE:
			return TEXT("ConnectionError");
		case CURLE_SSL_CONNECT_ERROR:
		case CURLE_PEER_FAILED_VERIFICATION:
		case CURLE_SSL_CERTPROBLEM:
		case CURLE_SSL_CIPHER:
		case CURLE_SSL_CACERT_BADFILE:
		case CURLE_SSL_ISSUER_ERROR:
		case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
		case CURLE_SSL_INVALIDCERTSTATUS:
		case CURLE_SSL_CLIENTCERT:
			return TEXT("TlsError");
		case CURLE_UNSUPPORTED_PROTOCOL:
		case CURLE_URL_MALFORMAT:
			return TEXT("BadUrl");
		default:
			return TEXT("Other");
		}
	}

	/** CURLOPT_WRITEFUNCTION: the body goes into the cap as it downloads; a short count aborts the transfer (S4). */
	size_t onBody(char* Data, size_t Size, size_t Count, void* User)
	{
		FCurlTransport::FPending& Pending = *static_cast<FCurlTransport::FPending*>(User);
		const size_t Bytes = Size * Count;
		Pending.CappedBody.Serialize(Data, int64(Bytes));
		return Pending.CappedBody.Overflowed() ? 0 : Bytes;
	}

	/** CURLOPT_HEADERFUNCTION: keeps only Retry-After, from the last status line on (a 1xx interim answer comes first). */
	size_t onHeader(char* Data, size_t Size, size_t Count, void* User)
	{
		FCurlTransport::FPending& Pending = *static_cast<FCurlTransport::FPending*>(User);
		const size_t Bytes = Size * Count;
		static const char RetryAfter[] = "retry-after:";
		const size_t NameBytes = sizeof(RetryAfter) - 1;
		if (startsWithNoCase(Data, Bytes, "http/"))
		{
			Pending.RetryAfter.Reset();
		}
		else if (Bytes <= NameBytes + 200 && startsWithNoCase(Data, Bytes, RetryAfter))
		{
			size_t End = Bytes;
			while (End > NameBytes && (Data[End - 1] == '\r' || Data[End - 1] == '\n'))
			{
				--End;
			}
			Pending.RetryAfter = fromUtf8(Data + NameBytes, int32(End - NameBytes)).TrimStartAndEnd();
		}
		return Bytes;
	}

	/**
	 * CURLOPT_SSL_CTX_FUNCTION: the trusted roots, as the engine adds them (CurlHttp.cpp:55-60). OpenSSL then checks the
	 * chain and the host name (CURLOPT_SSL_VERIFYPEER 1, CURLOPT_SSL_VERIFYHOST 2). The engine's certificate pinning
	 * callback (CurlHttp.cpp:31-53, :61-66) is deliberately not mirrored (controller decision, Task 1e): pins configured
	 * for the engine are not applied to the mod's requests (recorded for the Task 10 security review).
	 */
	CURLcode onSslContext(CURL*, void* SslContext, void*)
	{
		FSslModule::Get().GetCertificateManager().AddCertificatesToSslContext(static_cast<SSL_CTX*>(SslContext));
		return CURLE_OK;
	}

	template <typename OptionValue>
	bool setOption(CURL* Easy, CURLoption Option, OptionValue Value)
	{
		return curl_easy_setopt(Easy, Option, Value) == CURLE_OK;
	}
}

SatisDash::FCurlTransport::~FCurlTransport()
{
	CancelAll();
}

void SatisDash::FCurlTransport::Send(const FRequestSpec& Spec, FReplyHandler OnReply)
{
	TUniquePtr<FPending> Owned = MakeUnique<FPending>();
	FPending& Pending = *Owned;
	Pending.Url = Spec.Url;
	Pending.OnReply = MoveTemp(OnReply);
	InFlight.Add(MoveTemp(Owned));
	if (!bTicking)
	{
		bTicking = true;
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("SatisDashCurl"), 0.0f, [this](float) { return Tick(); });
	}
	if (!Start(Pending, Spec))
	{
		// Answered on the next tick, like every other reply: never inside Send.
		Release(Pending);
		Pending.Raw = FRawReply();
		Pending.Raw.Failure = TEXT("NotStarted");
		Pending.bDone = true;
	}
}

bool SatisDash::FCurlTransport::Start(FPending& Pending, const FRequestSpec& Spec)
{
	// S1: https, or plain http only to 127.0.0.1/localhost (the harness's backends), checked again here before any curl call.
	bool bPlainHttp = false;
	if (!IsAllowedRequestUrl(Spec.Url, bPlainHttp))
	{
		return false;
	}
	if (Multi == nullptr)
	{
		Multi = curl_multi_init();
		if (Multi == nullptr)
		{
			return false;
		}
	}
	// A line break in a header would let it inject another (the secret comes from a file); such a request is never sent.
	for (const TPair<FString, FString>& Header : Spec.Headers)
	{
		if (hasLineBreak(Header.Key) || hasLineBreak(Header.Value))
		{
			return false;
		}
		curl_slist* Next = curl_slist_append(Pending.Headers, TCHAR_TO_UTF8(*(Header.Key + TEXT(": ") + Header.Value)));
		if (Next == nullptr)
		{
			return false;
		}
		Pending.Headers = Next;
	}

	CURL* Easy = curl_easy_init();
	if (Easy == nullptr)
	{
		return false;
	}
	Pending.Easy = Easy;

	// A NaN fails both comparisons and takes the default too.
	const double Seconds = (Spec.TimeoutSeconds > 0.f && Spec.TimeoutSeconds <= 600.f) ? double(Spec.TimeoutSeconds) : double(RequestTimeoutSeconds);
	const long TimeoutMs = long(Seconds * 1000.0);

	// Every option must take; any refusal fails the request closed. Never CURLOPT_VERBOSE or CURLOPT_DEBUGFUNCTION: they
	// would print the Authorization header.
	bool bSet = setOption(Easy, CURLOPT_URL, TCHAR_TO_UTF8(*Spec.Url));
	// Proxies, chosen deliberately (security review F3). Plain http (loopback only): an empty CURLOPT_PROXY turns every
	// proxy off, the environment's included, so the Bearer header never goes to a proxy in clear. https: the
	// environment's proxy variables are still honoured (a host may need them); that is a CONNECT tunnel, and TLS still
	// runs end to end with the backend, so the proxy never sees the secret or the body.
	if (bPlainHttp)
	{
		bSet = bSet && setOption(Easy, CURLOPT_PROXY, "");
	}
	// S3: a redirect is never followed. The protocol is https, or http only for a loopback URL (S1 allows it for the
	// harness; IsAllowedRequestUrl checked the host above); a redirect could only ever be to https, and none is followed.
	bSet = bSet && setOption(Easy, CURLOPT_FOLLOWLOCATION, 0L);
	bSet = bSet && setOption(Easy, CURLOPT_MAXREDIRS, 0L);
	bSet = bSet && setOption(Easy, CURLOPT_PROTOCOLS_STR, bPlainHttp ? "http" : "https");
	bSet = bSet && setOption(Easy, CURLOPT_REDIR_PROTOCOLS_STR, "https");
	// S1, S2: the peer's chain and name are verified; TLS 1.2 is the minimum, with no maximum.
	bSet = bSet && setOption(Easy, CURLOPT_SSL_VERIFYPEER, 1L);
	bSet = bSet && setOption(Easy, CURLOPT_SSL_VERIFYHOST, 2L);
	bSet = bSet && setOption(Easy, CURLOPT_SSLVERSION, long(CURL_SSLVERSION_TLSv1_2));
	// Certificates as the engine does them with WITH_SSL (CurlHttp.cpp:181-186): no CA file, the SSL module's roots.
	bSet = bSet && setOption(Easy, CURLOPT_CAINFO, static_cast<const char*>(nullptr));
	bSet = bSet && setOption(Easy, CURLOPT_SSL_CTX_FUNCTION, &onSslContext);
	bSet = bSet && setOption(Easy, CURLOPT_NOSIGNAL, 1L);
	// HTTP/1.1, as UE's requests use by default (CurlHttp.cpp:107-114).
	bSet = bSet && setOption(Easy, CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
	bSet = bSet && setOption(Easy, CURLOPT_TIMEOUT_MS, TimeoutMs);
	bSet = bSet && setOption(Easy, CURLOPT_CONNECTTIMEOUT_MS, TimeoutMs);
	bSet = bSet && setOption(Easy, CURLOPT_WRITEFUNCTION, &onBody);
	bSet = bSet && setOption(Easy, CURLOPT_WRITEDATA, static_cast<void*>(&Pending));
	bSet = bSet && setOption(Easy, CURLOPT_HEADERFUNCTION, &onHeader);
	bSet = bSet && setOption(Easy, CURLOPT_HEADERDATA, static_cast<void*>(&Pending));
	if (Pending.Headers != nullptr)
	{
		bSet = bSet && setOption(Easy, CURLOPT_HTTPHEADER, Pending.Headers);
	}
	if (Spec.Verb.Equals(TEXT("GET"), ESearchCase::CaseSensitive))
	{
		bSet = bSet && setOption(Easy, CURLOPT_HTTPGET, 1L);
	}
	else
	{
		// The body is copied here and outlives the transfer (CURLOPT_POSTFIELDS is read in place).
		Pending.PostBody = Spec.Body;
		static const char Empty[] = "";
		const char* Fields = Pending.PostBody.Num() > 0 ? reinterpret_cast<const char*>(Pending.PostBody.GetData()) : Empty;
		bSet = bSet && setOption(Easy, CURLOPT_POST, 1L);
		bSet = bSet && setOption(Easy, CURLOPT_POSTFIELDSIZE, long(Pending.PostBody.Num()));
		bSet = bSet && setOption(Easy, CURLOPT_POSTFIELDS, Fields);
		if (!Spec.Verb.Equals(TEXT("POST"), ESearchCase::CaseSensitive))
		{
			bSet = bSet && setOption(Easy, CURLOPT_CUSTOMREQUEST, TCHAR_TO_UTF8(*Spec.Verb));
		}
	}
	if (!bSet)
	{
		return false;
	}
	return curl_multi_add_handle(Multi, Easy) == CURLM_OK;
}

bool SatisDash::FCurlTransport::Tick()
{
	// Without a multi handle (curl_multi_init failed) there is nothing to pump, but a queued NotStarted reply is still
	// delivered below: every Send gets exactly one reply.
	if (Multi != nullptr)
	{
		int Running = 0;
		if (curl_multi_perform(Multi, &Running) != CURLM_OK)
		{
			// The multi handle is broken: every request still out fails now (Code 0), rather than waiting on timeouts that
			// only perform drives. The next Send starts a fresh multi handle.
			for (const TUniquePtr<FPending>& Each : InFlight)
			{
				if (!Each->bDone)
				{
					Release(*Each);
					Each->Raw = FRawReply();
					Each->Raw.Failure = TEXT("Other");
					Each->bDone = true;
				}
			}
			curl_multi_cleanup(Multi);
			Multi = nullptr;
		}
		else
		{
			int Left = 0;
			while (CURLMsg* Message = curl_multi_info_read(Multi, &Left))
			{
				if (Message->msg != CURLMSG_DONE)
				{
					continue;
				}
				for (const TUniquePtr<FPending>& Each : InFlight)
				{
					if (Each->Easy == Message->easy_handle)
					{
						Complete(*Each, int32(Message->data.result));
						break;
					}
				}
			}
		}
	}

	// Most ticks finish nothing; then nothing is moved (follow-up 1: footprint).
	bool bAnyDone = false;
	for (const TUniquePtr<FPending>& Each : InFlight)
	{
		bAnyDone = bAnyDone || Each->bDone;
	}
	if (bAnyDone)
	{
		// Handlers run last, on requests already taken out of InFlight: one may Send again, or CancelAll.
		TArray<TUniquePtr<FPending>> Done;
		TArray<TUniquePtr<FPending>> Open;
		for (TUniquePtr<FPending>& Each : InFlight)
		{
			(Each->bDone ? Done : Open).Add(MoveTemp(Each));
		}
		InFlight = MoveTemp(Open);
		const uint32 Started = Generation;
		for (const TUniquePtr<FPending>& Each : Done)
		{
			if (Generation != Started)
			{
				break; // CancelAll ran in a handler: nothing more is delivered.
			}
			Deliver(*Each);
		}
		if (Generation != Started)
		{
			return false;
		}
	}
	// Idle: the ticker stops until the next Send (follow-up 1: it used to run every frame for the whole uptime). The multi
	// handle stays, with its connection cache, for the next request; nothing in it needs pumping while no request is out.
	if (InFlight.Num() == 0)
	{
		bTicking = false;
		return false;
	}
	return true;
}

void SatisDash::FCurlTransport::Complete(FPending& Pending, int32 Result)
{
	FRawReply Raw;
	CURL* Easy = Pending.Easy;
	long Code = 0;
	if (Result == CURLE_OK && curl_easy_getinfo(Easy, CURLINFO_RESPONSE_CODE, &Code) == CURLE_OK && Code > 0)
	{
		Raw.bProcessed = true;
		Raw.Code = int32(Code);
	}
	char* ContentType = nullptr;
	if (curl_easy_getinfo(Easy, CURLINFO_CONTENT_TYPE, &ContentType) == CURLE_OK && ContentType != nullptr)
	{
		Raw.ContentType = fromUtf8(ContentType, int32(strlen(ContentType)));
	}
	// Redirects are never followed, so this is the URL asked; MakeReply still checks it.
	char* EffectiveUrl = nullptr;
	if (curl_easy_getinfo(Easy, CURLINFO_EFFECTIVE_URL, &EffectiveUrl) == CURLE_OK && EffectiveUrl != nullptr)
	{
		Raw.EffectiveUrl = fromUtf8(EffectiveUrl, int32(strlen(EffectiveUrl)));
	}
	Raw.RetryAfter = Pending.RetryAfter;
	Raw.Failure = failureWord(CURLcode(Result));
	Release(Pending);
	Pending.Raw = Raw;
	Pending.bDone = true;
}

void SatisDash::FCurlTransport::Release(FPending& Pending)
{
	if (Pending.Easy != nullptr)
	{
		if (Multi != nullptr)
		{
			curl_multi_remove_handle(Multi, Pending.Easy);
		}
		curl_easy_cleanup(Pending.Easy);
		Pending.Easy = nullptr;
	}
	if (Pending.Headers != nullptr)
	{
		curl_slist_free_all(Pending.Headers);
		Pending.Headers = nullptr;
	}
}

void SatisDash::FCurlTransport::Deliver(FPending& Pending)
{
	EReplyRefusal Refusal = EReplyRefusal::None;
	const FHttpReply Reply = MakeReply(Pending.Raw, Pending.Url, Pending.CappedBody, Refusal);
	const double Now = FPlatformTime::Seconds();
	// S9: a refused response and a TLS failure are logged, once per kind per ten minutes; never a header and never more
	// of the URL than its origin (the secret is only in the Authorization header, S7). A failure to reach a backend is
	// keyed by its origin too, so a newly configured backend's failure is logged even if the old one's was just now.
	if (Refusal == EReplyRefusal::Redirected && RefusalLog.ShouldLog(TEXT("redirect"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("The backend redirected a request; its answer was refused (redirects are never accepted)"));
	}
	else if (Refusal == EReplyRefusal::Oversized && RefusalLog.ShouldLog(TEXT("oversized"), Now))
	{
		UE_LOG(LogSatisDash, Warning, TEXT("A backend response was over %d bytes; refused it"), MaxResponseBytes);
	}
	else if (Refusal == EReplyRefusal::NoAnswer)
	{
		const FString Origin = originOf(Pending.Url);
		if (RefusalLog.ShouldLog(Pending.Raw.Failure + TEXT(" ") + Origin, Now))
		{
			UE_LOG(LogSatisDash, Warning, TEXT("Couldn't reach the backend at %s (%s)"), *Origin, *Pending.Raw.Failure);
		}
	}
	const FReplyHandler& onReply = Pending.OnReply;
	onReply(Reply);
}

void SatisDash::FCurlTransport::CancelAll()
{
	++Generation;
	if (bTicking)
	{
		FTSTicker::RemoveTicker(TickHandle);
		bTicking = false;
	}
	for (const TUniquePtr<FPending>& Each : InFlight)
	{
		Release(*Each);
	}
	InFlight.Reset();
	if (Multi != nullptr)
	{
		curl_multi_cleanup(Multi);
		Multi = nullptr;
	}
}
