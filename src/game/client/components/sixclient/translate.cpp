#include "translate.h"

#include <base/dbg.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/shared/json.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/protocol.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
constexpr int64_t TRANSLATE_TIMEOUT = 20;

void UrlEncode(const char *pText, char *pOut, size_t Length)
{
	if(!pText || Length == 0)
		return;

	size_t OutPos = 0;
	for(const char *p = pText; *p && OutPos + 1 < Length; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
		{
			pOut[OutPos++] = c;
		}
		else
		{
			if(OutPos + 3 >= Length)
				break;
			snprintf(pOut + OutPos, 4, "%%%02X", c);
			OutPos += 3;
		}
	}
	pOut[OutPos] = '\0';
}

void NormalizeLanguageCode(const char *pLanguage, char *pOut, size_t Size)
{
	if(!pOut || Size == 0)
		return;
	pOut[0] = '\0';
	if(!pLanguage || !pLanguage[0])
		return;

	char aLower[32] = "";
	int Length = 0;
	for(const char *p = pLanguage; *p && Length < (int)sizeof(aLower) - 1; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(c == '-' || c == '_' || c == ';' || c == ',' || c == '|' || isspace(c))
			break;
		if(isalnum(c))
			aLower[Length++] = (char)tolower(c);
	}
	aLower[Length] = '\0';

	struct SMapping
	{
		const char *m_pFrom;
		const char *m_pTo;
	};
	static const SMapping s_aMappings[] = {
		{"auto", "auto"},
		{"ru", "ru"}, {"russian", "ru"},
		{"en", "en"}, {"english", "en"},
		{"de", "de"}, {"german", "de"},
		{"fr", "fr"}, {"french", "fr"},
		{"es", "es"}, {"spanish", "es"},
		{"pl", "pl"}, {"polish", "pl"},
		{"zh", "zh"}, {"cn", "zh"}, {"chinese", "zh"},
		{"pt", "pt"}, {"portuguese", "pt"}, {"brazilian", "pt"},
		{"tr", "tr"}, {"turkish", "tr"},
	};
	for(const SMapping &Mapping : s_aMappings)
	{
		if(str_comp(aLower, Mapping.m_pFrom) == 0)
		{
			str_copy(pOut, Mapping.m_pTo, Size);
			return;
		}
	}
}

bool IsAutoLanguage(const char *pLanguage)
{
	return !pLanguage || !pLanguage[0] || str_comp_nocase(pLanguage, "auto") == 0;
}

bool LanguagesEqual(const char *pA, const char *pB)
{
	if(IsAutoLanguage(pA) || IsAutoLanguage(pB))
		return false;
	return str_comp_nocase(pA, pB) == 0;
}

bool IsApostropheCodepoint(int Code)
{
	return Code == '\'' || Code == 0x2018 || Code == 0x2019 || Code == 0x02BC;
}

void StripApostrophesFromTranslatedText(char *pText)
{
	char *pWrite = pText;
	const char *pRead = pText;
	while(*pRead)
	{
		const char *pBefore = pRead;
		const int Code = str_utf8_decode(&pRead);
		if(Code < 0)
			break;
		if(IsApostropheCodepoint(Code))
			continue;
		while(pBefore < pRead)
			*pWrite++ = *pBefore++;
	}
	*pWrite = '\0';
}

void NormalizeTranslatedText(char *pText, size_t Size)
{
	if(!pText || !pText[0])
		return;

	std::string Decoded;
	bool HasEscape = false;
	for(size_t i = 0; pText[i]; ++i)
	{
		if(pText[i] == '%' && pText[i + 1] && pText[i + 2] && isxdigit((unsigned char)pText[i + 1]) && isxdigit((unsigned char)pText[i + 2]))
		{
			const char aHex[] = {pText[i + 1], pText[i + 2], '\0'};
			Decoded.push_back((char)strtol(aHex, nullptr, 16));
			HasEscape = true;
			i += 2;
		}
		else if(pText[i] == '+')
			Decoded.push_back(' ');
		else
			Decoded.push_back(pText[i]);
	}
	if(HasEscape)
		str_copy(pText, Decoded.c_str(), Size);
}

bool ExtractOutgoingCommand(const char *pText, const char *pCommand, const char **ppArguments)
{
	if(!pText || pText[0] != '/' || !ppArguments)
		return false;
	const char *pStart = pText + 1;
	const char *pEnd = pStart;
	while(*pEnd && !isspace((unsigned char)*pEnd))
		++pEnd;
	if((size_t)(pEnd - pStart) != (size_t)str_length(pCommand) || str_comp_nocase(std::string(pStart, pEnd - pStart).c_str(), pCommand) != 0)
		return false;
	const char *pArgumentsStart = pEnd;
	while(isspace((unsigned char)*pArgumentsStart))
		++pArgumentsStart;
	*ppArguments = pArgumentsStart;
	return *pArgumentsStart != '\0';
}

bool ExtractOutgoingTranslatableText(const CGameClient &GameClient, const char *pText, char *pPrefix, size_t PrefixSize, char *pBody, size_t BodySize)
{
	pPrefix[0] = '\0';
	pBody[0] = '\0';
	if(!pText || !pText[0])
		return false;

	if(pText[0] == '/')
	{
		const char *pArguments = nullptr;
		if(ExtractOutgoingCommand(pText, "c", &pArguments))
		{
			const int PrefixLength = (int)(pArguments - pText);
			str_copy(pPrefix, std::string(pText, PrefixLength).c_str(), PrefixSize);
			str_copy(pBody, pArguments, BodySize);
			return pBody[0] != '\0';
		}

		if(ExtractOutgoingCommand(pText, "w", &pArguments) || ExtractOutgoingCommand(pText, "whisper", &pArguments))
		{
			const char *pBodyStart = str_find(pArguments, " ");
			if(!pBodyStart)
				return false;
			while(isspace((unsigned char)*pBodyStart))
				++pBodyStart;
			if(!pBodyStart[0])
				return false;
			const int PrefixLength = (int)(pBodyStart - pText);
			str_copy(pPrefix, std::string(pText, PrefixLength).c_str(), PrefixSize);
			str_copy(pBody, pBodyStart, BodySize);
			return true;
		}
		return false;
	}

	const char *pColon = str_find(pText, ":");
	if(pColon && pColon != pText)
	{
		const int NameLength = (int)(pColon - pText);
		for(const auto &Client : GameClient.m_aClients)
		{
			if(!Client.m_Active || str_length(Client.m_aName) != NameLength)
				continue;
			char aName[64] = "";
			str_copy(aName, std::string(pText, NameLength).c_str(), sizeof(aName));
			if(str_comp_nocase(Client.m_aName, aName) != 0)
				continue;
			const char *pBodyStart = pColon + 1;
			while(isspace((unsigned char)*pBodyStart))
				++pBodyStart;
			if(!pBodyStart[0])
				return false;
			str_copy(pPrefix, std::string(pText, pBodyStart - pText).c_str(), PrefixSize);
			str_copy(pBody, pBodyStart, BodySize);
			return true;
		}
	}

	str_copy(pBody, pText, BodySize);
	return pBody[0] != '\0';
}
}

const char *ITranslateBackend::EncodeTarget(const char *pTarget) const
{
	return pTarget && pTarget[0] ? pTarget : DefaultConfig::TcTranslateTarget;
}

class ITranslateBackendHttp : public ITranslateBackend
{
protected:
	using ITranslateBackend::ITranslateBackend;
	std::shared_ptr<IHttpRequest> m_pHttpRequest;
	virtual bool ParseResponse(CTranslateResponse &Out) = 0;
	virtual bool ParseHttpError() const { return false; }

	static void NormalizeEndpointUrl(const char *pUrl, char *pOut, size_t OutSize)
	{
		if(!pUrl || !pUrl[0])
		{
			str_copy(pOut, "", OutSize);
			return;
		}
		while(*pUrl && isspace((unsigned char)*pUrl))
			++pUrl;
		if(str_startswith(pUrl, "http://") || str_startswith(pUrl, "https://"))
			str_copy(pOut, pUrl, OutSize);
		else
			str_format(pOut, OutSize, "http://%s", pUrl);
	}

	void CreateHttpRequest(IHttp &Http, const char *pUrl)
	{
		char aUrl[4096];
		NormalizeEndpointUrl(pUrl, aUrl, sizeof(aUrl));
		auto pRequest = HttpGet(aUrl);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{20000, 0, 500, 10});
		pRequest->AllowHttp(true);
		m_pHttpRequest = std::move(pRequest);
		Http.Run(m_pHttpRequest);
	}

	void CreateHttpRequestPost(IHttp &Http, const char *pUrl, const char *pBody)
	{
		char aUrl[4096];
		NormalizeEndpointUrl(pUrl, aUrl, sizeof(aUrl));
		auto pRequest = HttpGet(aUrl);
		pRequest->LogProgress(HTTPLOG::FAILURE);
		pRequest->FailOnErrorStatus(false);
		pRequest->Timeout(CTimeout{20000, 0, 500, 10});
		pRequest->AllowHttp(true);
		pRequest->HeaderString("Content-Type", "application/x-www-form-urlencoded");
		pRequest->Post((const unsigned char *)pBody, str_length(pBody));
		m_pHttpRequest = std::move(pRequest);
		Http.Run(m_pHttpRequest);
	}

public:
	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		dbg_assert(m_pHttpRequest != nullptr, "m_pHttpRequest is nullptr");
		if(m_pHttpRequest->State() == EHttpState::RUNNING || m_pHttpRequest->State() == EHttpState::QUEUED)
			return std::nullopt;
		if(m_pHttpRequest->State() == EHttpState::ABORTED)
		{
			str_copy(Out.m_Text, "Aborted");
			return false;
		}
		if(m_pHttpRequest->State() != EHttpState::DONE)
		{
			str_copy(Out.m_Text, "Curl error, see console");
			return false;
		}
		if(m_pHttpRequest->StatusCode() != 200 && !ParseHttpError())
		{
			str_format(Out.m_Text, sizeof(Out.m_Text), "Got http code %d", m_pHttpRequest->StatusCode());
			return false;
		}
		return ParseResponse(Out);
	}

	~ITranslateBackendHttp() override
	{
		if(m_pHttpRequest)
			m_pHttpRequest->Abort();
	}
};

class CTranslateBackendLibretranslate : public ITranslateBackendHttp
{
	bool ParseResponseJson(const json_value *pObj, CTranslateResponse &Out)
	{
		if(!pObj || pObj->type != json_object)
		{
			str_copy(Out.m_Text, "Response is not a JSON object");
			return false;
		}
		const json_value *pError = json_object_get(pObj, "error");
		if(pError != &json_value_none)
		{
			if(pError->type == json_string)
				str_copy(Out.m_Text, pError->u.string.ptr);
			else
				str_copy(Out.m_Text, "LibreTranslate error");
			return false;
		}
		const json_value *pTranslated = json_object_get(pObj, "translatedText");
		if(pTranslated == &json_value_none || pTranslated->type != json_string)
		{
			str_copy(Out.m_Text, "No translatedText");
			return false;
		}
		str_copy(Out.m_Text, pTranslated->u.string.ptr);
		const json_value *pDetected = json_object_get(pObj, "detectedLanguage");
		if(pDetected != &json_value_none && pDetected->type == json_object)
		{
			const json_value *pLanguage = json_object_get(pDetected, "language");
			if(pLanguage != &json_value_none && pLanguage->type == json_string)
				str_copy(Out.m_Language, pLanguage->u.string.ptr);
		}
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		return Out.m_Text[0] != '\0';
	}

	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pObj = m_pHttpRequest->ResultJson();
		const bool Result = ParseResponseJson(pObj, Out);
		json_value_free(pObj);
		return Result;
	}

	bool ParseHttpError() const override { return true; }

public:
	CTranslateBackendLibretranslate(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("q");
		Json.WriteStrValue(pText);
		Json.WriteAttribute("source");
		Json.WriteStrValue(IsAutoLanguage(pSource) ? "auto" : pSource);
		Json.WriteAttribute("target");
		Json.WriteStrValue(EncodeTarget(pTarget));
		Json.WriteAttribute("format");
		Json.WriteStrValue("text");
		if(g_Config.m_TcTranslateKey[0])
		{
			Json.WriteAttribute("api_key");
			Json.WriteStrValue(g_Config.m_TcTranslateKey);
		}
		Json.EndObject();
		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] ? g_Config.m_TcTranslateEndpoint : "http://localhost:5000/translate");
		m_pHttpRequest->PostJson(Json.GetOutputString().c_str());
	}
};

class CTranslateBackendDeepL : public ITranslateBackendHttp
{
	static void EncodeDeepLLanguage(const char *pLanguage, char *pOut, size_t Size, bool Target)
	{
		if(!pOut || Size == 0)
			return;
		pOut[0] = '\0';
		char aNorm[16] = "";
		NormalizeLanguageCode(pLanguage, aNorm, sizeof(aNorm));
		if(!aNorm[0] || str_comp(aNorm, "auto") == 0)
			return;
		if(str_comp(aNorm, "en") == 0)
		{
			str_copy(pOut, Target ? "EN-US" : "EN", Size);
			return;
		}
		if(str_comp(aNorm, "pt") == 0)
		{
			str_copy(pOut, "PT-BR", Size);
			return;
		}
		if(str_comp(aNorm, "uk") == 0)
		{
			str_copy(pOut, "UK", Size);
			return;
		}
		if(str_comp(aNorm, "no") == 0)
		{
			str_copy(pOut, "NB", Size);
			return;
		}
		if(str_length(aNorm) >= 2)
		{
			pOut[0] = (char)toupper((unsigned char)aNorm[0]);
			pOut[1] = (char)toupper((unsigned char)aNorm[1]);
			pOut[2] = '\0';
		}
	}

	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot || pRoot->type != json_object)
		{
			str_copy(Out.m_Text, "Unexpected response format");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pMessage = json_object_get(pRoot, "message");
		if(pMessage != &json_value_none && pMessage->type == json_string)
		{
			str_copy(Out.m_Text, pMessage->u.string.ptr);
			json_value_free(pRoot);
			return false;
		}
		const json_value *pTranslations = json_object_get(pRoot, "translations");
		if(pTranslations == &json_value_none || pTranslations->type != json_array || json_array_length(pTranslations) < 1)
		{
			str_copy(Out.m_Text, "No translations");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pEntry = json_array_get(pTranslations, 0);
		if(!pEntry || pEntry->type != json_object)
		{
			str_copy(Out.m_Text, "Invalid translation entry");
			json_value_free(pRoot);
			return false;
		}
		const json_value *pText = json_object_get(pEntry, "text");
		if(pText == &json_value_none || pText->type != json_string)
		{
			str_copy(Out.m_Text, "No translation text");
			json_value_free(pRoot);
			return false;
		}
		str_copy(Out.m_Text, pText->u.string.ptr);
		const json_value *pDetected = json_object_get(pEntry, "detected_source_language");
		if(pDetected != &json_value_none && pDetected->type == json_string)
			str_copy(Out.m_Language, pDetected->u.string.ptr);
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		json_value_free(pRoot);
		return Out.m_Text[0] != '\0';
	}

	bool ParseHttpError() const override { return true; }

public:
	CTranslateBackendDeepL(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName)
	{
		char aTarget[16] = "";
		char aSource[16] = "";
		EncodeDeepLLanguage(pTarget, aTarget, sizeof(aTarget), true);
		EncodeDeepLLanguage(pSource, aSource, sizeof(aSource), false);
		if(!aTarget[0])
		{
			str_copy(aTarget, "EN-US", sizeof(aTarget));
		}

		CJsonStringWriter Json;
		Json.BeginObject();
		Json.WriteAttribute("text");
		Json.BeginArray();
		Json.WriteStrValue(pText);
		Json.EndArray();
		if(aSource[0])
		{
			Json.WriteAttribute("source_lang");
			Json.WriteStrValue(aSource);
		}
		Json.WriteAttribute("target_lang");
		Json.WriteStrValue(aTarget);
		Json.EndObject();

		CreateHttpRequest(Http, g_Config.m_TcTranslateEndpoint[0] ? g_Config.m_TcTranslateEndpoint : "https://api-free.deepl.com/v2/translate");
		if(g_Config.m_TcTranslateKey[0])
		{
			char aAuth[280];
			str_format(aAuth, sizeof(aAuth), "DeepL-Auth-Key %s", g_Config.m_TcTranslateKey);
			m_pHttpRequest->HeaderString("Authorization", aAuth);
		}
		m_pHttpRequest->PostJson(Json.GetOutputString().c_str());
	}
};

// Google's unofficial translate_a endpoint is captcha-blocked from many networks.
// Keep the "google" backend id for existing configs, but talk to MyMemory instead.
class CTranslateBackendGoogle : public ITranslateBackendHttp
{
	std::string m_SourceText;
	std::string m_Target;

	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot || pRoot->type != json_object)
		{
			str_copy(Out.m_Text, "Unexpected response format");
			json_value_free(pRoot);
			return false;
		}

		// responseStatus is an integer on success but a string ("403") on errors.
		const json_value *pStatus = json_object_get(pRoot, "responseStatus");
		const int Status = pStatus->type == json_integer ? (int)pStatus->u.integer : (pStatus->type == json_string ? str_toint(pStatus->u.string.ptr) : 0);
		const json_value *pDetails = json_object_get(pRoot, "responseDetails");
		const char *pDetailsText = pDetails->type == json_string ? pDetails->u.string.ptr : "";
		// Autodetected source equal to the target is rejected instead of echoed back. Treat it as an
		// untranslated line: incoming chat then hides it (same text) and outgoing chat sends it as typed.
		if(str_find_nocase(pDetailsText, "TWO DISTINCT LANGUAGES"))
		{
			str_copy(Out.m_Text, m_SourceText.c_str());
			str_copy(Out.m_Language, m_Target.c_str());
			json_value_free(pRoot);
			return true;
		}

		const json_value *pData = json_object_get(pRoot, "responseData");
		const json_value *pTranslated = pData->type == json_object ? json_object_get(pData, "translatedText") : &json_value_none;
		// Quota warnings arrive as a "translation" with status 200.
		if(Status != 200 || pTranslated->type != json_string || !pTranslated->u.string.ptr[0] || str_startswith(pTranslated->u.string.ptr, "MYMEMORY WARNING"))
		{
			const char *pError = pDetailsText[0] ? pDetailsText : (pTranslated->type == json_string && pTranslated->u.string.ptr[0] ? pTranslated->u.string.ptr : "Empty translation");
			str_copy(Out.m_Text, pError);
			json_value_free(pRoot);
			return false;
		}

		str_copy(Out.m_Text, pTranslated->u.string.ptr);
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));

		// Autodetect reports the language in responseData; matches[].source is false for machine translations.
		const json_value *pDetected = json_object_get(pData, "detectedLanguage");
		if(pDetected->type == json_string && pDetected->u.string.ptr[0])
		{
			char aDetected[16] = "";
			NormalizeLanguageCode(pDetected->u.string.ptr, aDetected, sizeof(aDetected));
			if(aDetected[0])
				str_copy(Out.m_Language, aDetected);
		}

		json_value_free(pRoot);
		return Out.m_Text[0] != '\0';
	}

public:
	CTranslateBackendGoogle(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName),
		m_SourceText(pText),
		m_Target(EncodeTarget(pTarget))
	{
		char aEncoded[2048];
		UrlEncode(pText, aEncoded, sizeof(aEncoded));
		const char *pSl = IsAutoLanguage(pSource) ? "Autodetect" : pSource;
		char aUrl[2560];
		str_format(aUrl, sizeof(aUrl), "https://api.mymemory.translated.net/get?q=%s&langpair=%s%%7C%s", aEncoded, pSl, m_Target.c_str());
		CreateHttpRequest(Http, aUrl);
	}
};

// Google Translate itself; MyMemory above answers from a crowd memory and often sends back a
// lightly edited copy of English lines. Where Google is blocked, MyMemory takes over.
class CTranslateBackendGoogleTranslate : public ITranslateBackendHttp
{
	IHttp &m_Http;
	std::string m_Text;
	std::string m_Source;
	std::string m_Target;
	std::string m_Name;
	std::unique_ptr<CTranslateBackendGoogle> m_pFallback;

	bool ParseResponse(CTranslateResponse &Out) override
	{
		json_value *pRoot = m_pHttpRequest->ResultJson();
		if(!pRoot || pRoot->type != json_array || pRoot->u.array.length < 1)
		{
			json_value_free(pRoot);
			return false;
		}
		std::string Text;
		const json_value *pSentences = pRoot->u.array.values[0];
		if(pSentences->type == json_array)
			for(unsigned i = 0; i < pSentences->u.array.length; i++)
			{
				const json_value *pSentence = pSentences->u.array.values[i];
				if(pSentence->type == json_array && pSentence->u.array.length > 0 && pSentence->u.array.values[0]->type == json_string)
					Text += pSentence->u.array.values[0]->u.string.ptr;
			}
		if(pRoot->u.array.length > 2 && pRoot->u.array.values[2]->type == json_string)
		{
			char aDetected[16] = "";
			NormalizeLanguageCode(pRoot->u.array.values[2]->u.string.ptr, aDetected, sizeof(aDetected));
			str_copy(Out.m_Language, aDetected);
		}
		json_value_free(pRoot);
		if(Text.empty())
			return false;
		str_copy(Out.m_Text, Text.c_str());
		NormalizeTranslatedText(Out.m_Text, sizeof(Out.m_Text));
		return true;
	}

public:
	CTranslateBackendGoogleTranslate(IHttp &Http, const char *pText, const char *pSource, const char *pTarget, const char *pName) :
		ITranslateBackendHttp(pName),
		m_Http(Http),
		m_Text(pText),
		m_Source(pSource ? pSource : ""),
		m_Target(pTarget ? pTarget : ""),
		m_Name(pName ? pName : "")
	{
		char aEncoded[2048];
		UrlEncode(pText, aEncoded, sizeof(aEncoded));
		char aUrl[2560];
		str_format(aUrl, sizeof(aUrl), "https://translate.googleapis.com/translate_a/single?client=gtx&dt=t&sl=%s&tl=%s&q=%s", IsAutoLanguage(pSource) ? "auto" : pSource, pTarget, aEncoded);
		CreateHttpRequest(Http, aUrl);
	}

	std::optional<bool> Update(CTranslateResponse &Out) override
	{
		if(m_pFallback)
			return m_pFallback->Update(Out);
		const std::optional<bool> Result = ITranslateBackendHttp::Update(Out);
		if(!Result.has_value() || *Result)
			return Result;
		log_info("translate", "Google Translate didn't answer, using MyMemory");
		Out = CTranslateResponse();
		m_pFallback = std::make_unique<CTranslateBackendGoogle>(m_Http, m_Text.c_str(), m_Source.c_str(), m_Target.c_str(), m_Name.c_str());
		return std::nullopt;
	}
};

static const std::vector<STranslateLanguage> s_vGoogleLanguages = {
	{"Arabic", "ar"}, {"Bulgarian", "bg"}, {"Chinese (Simplified)", "zh"}, {"Chinese (Traditional)", "zh-TW"},
	{"Croatian", "hr"}, {"Czech", "cs"}, {"Danish", "da"}, {"Dutch", "nl"}, {"English", "en"},
	{"Estonian", "et"}, {"Finnish", "fi"}, {"French", "fr"}, {"German", "de"}, {"Greek", "el"},
	{"Hebrew", "he"}, {"Hindi", "hi"}, {"Hungarian", "hu"}, {"Indonesian", "id"}, {"Italian", "it"},
	{"Japanese", "ja"}, {"Korean", "ko"}, {"Latvian", "lv"}, {"Lithuanian", "lt"}, {"Norwegian", "no"},
	{"Persian", "fa"}, {"Polish", "pl"}, {"Portuguese", "pt"}, {"Romanian", "ro"}, {"Russian", "ru"},
	{"Serbian", "sr"}, {"Slovak", "sk"}, {"Slovenian", "sl"}, {"Spanish", "es"}, {"Swedish", "sv"},
	{"Thai", "th"}, {"Turkish", "tr"}, {"Ukrainian", "uk"}, {"Vietnamese", "vi"},
};

static const std::vector<STranslateLanguage> s_vLibretranslateLanguages = {
	{"Arabic", "ar"}, {"Chinese (Simplified)", "zh"}, {"Czech", "cs"}, {"Dutch", "nl"}, {"English", "en"},
	{"Finnish", "fi"}, {"French", "fr"}, {"German", "de"}, {"Greek", "el"}, {"Hebrew", "he"},
	{"Hindi", "hi"}, {"Hungarian", "hu"}, {"Indonesian", "id"}, {"Italian", "it"}, {"Japanese", "ja"},
	{"Korean", "ko"}, {"Polish", "pl"}, {"Portuguese", "pt"}, {"Russian", "ru"}, {"Spanish", "es"},
	{"Swedish", "sv"}, {"Turkish", "tr"}, {"Ukrainian", "uk"}, {"Vietnamese", "vi"},
};

static const std::vector<STranslateLanguage> s_vDeepLLanguages = {
	{"Arabic", "ar"}, {"Bulgarian", "bg"}, {"Chinese (Simplified)", "zh"}, {"Czech", "cs"}, {"Danish", "da"},
	{"Dutch", "nl"}, {"English", "en"}, {"Estonian", "et"}, {"Finnish", "fi"}, {"French", "fr"},
	{"German", "de"}, {"Greek", "el"}, {"Hungarian", "hu"}, {"Indonesian", "id"}, {"Italian", "it"},
	{"Japanese", "ja"}, {"Korean", "ko"}, {"Latvian", "lv"}, {"Lithuanian", "lt"}, {"Norwegian", "no"},
	{"Polish", "pl"}, {"Portuguese", "pt"}, {"Romanian", "ro"}, {"Russian", "ru"}, {"Slovak", "sk"},
	{"Slovenian", "sl"}, {"Spanish", "es"}, {"Swedish", "sv"}, {"Turkish", "tr"}, {"Ukrainian", "uk"},
};

const std::array<STranslateBackendInfo, 3> g_aTranslateBackends = {{
	{"google", "MyMemory", s_vGoogleLanguages, false},
	{"libretranslate", "LibreTranslate", s_vLibretranslateLanguages, true},
	{"deepl", "DeepL", s_vDeepLLanguages, true},
}};

int TranslateLanguageIndex(const std::vector<STranslateLanguage> &vLanguages, const char *pCode)
{
	for(size_t i = 0; i < vLanguages.size(); ++i)
		if(str_comp_nocase(pCode, vLanguages[i].m_pCode) == 0)
			return (int)i;
	for(size_t i = 0; i < vLanguages.size(); ++i)
		if(str_comp_nocase("en", vLanguages[i].m_pCode) == 0)
			return (int)i;
	return 0;
}

std::unique_ptr<ITranslateBackend> CTranslate::CreateBackend(const char *pText, const char *pSourceLanguage, const char *pTargetLanguage) const
{
	if(IsAutoLanguage(pTargetLanguage))
		return nullptr;
	for(const STranslateBackendInfo &Backend : g_aTranslateBackends)
	{
		if(str_comp_nocase(g_Config.m_TcTranslateBackend, Backend.m_pValue) != 0)
			continue;
		if(str_comp_nocase(Backend.m_pValue, "google") == 0)
			return std::make_unique<CTranslateBackendGoogleTranslate>(*Http(), pText, pSourceLanguage, pTargetLanguage, Backend.m_pName);
		if(str_comp_nocase(Backend.m_pValue, "deepl") == 0)
			return std::make_unique<CTranslateBackendDeepL>(*Http(), pText, pSourceLanguage, pTargetLanguage, Backend.m_pName);
		if(str_comp_nocase(Backend.m_pValue, "libretranslate") == 0)
			return std::make_unique<CTranslateBackendLibretranslate>(*Http(), pText, pSourceLanguage, pTargetLanguage, Backend.m_pName);
	}
	return nullptr;
}

const char *CTranslate::IncomingSourceLanguage() const { return g_Config.m_ScTranslateIncomingSource; }
const char *CTranslate::IncomingTargetLanguage() const { return g_Config.m_TcTranslateTarget; }
const char *CTranslate::OutgoingSourceLanguage() const { return g_Config.m_ScTranslateOutgoingSource; }
const char *CTranslate::OutgoingTargetLanguage() const { return g_Config.m_ScTranslateOutgoingTarget; }

bool CTranslate::IsIgnoredIncomingLanguage(const char *pLanguage) const
{
	char aLanguage[16] = "";
	NormalizeLanguageCode(pLanguage, aLanguage, sizeof(aLanguage));
	if(!aLanguage[0] || str_comp(aLanguage, "auto") == 0)
		return false;

	const char *pToken = g_Config.m_ScTranslateIncomingIgnoreLanguages;
	while(*pToken)
	{
		while(*pToken && (*pToken == ';' || *pToken == ',' || *pToken == '|' || isspace((unsigned char)*pToken)))
			++pToken;
		const char *pEnd = pToken;
		while(*pEnd && *pEnd != ';' && *pEnd != ',' && *pEnd != '|' && !isspace((unsigned char)*pEnd))
			++pEnd;
		char aToken[32] = "";
		str_copy(aToken, std::string(pToken, pEnd - pToken).c_str(), sizeof(aToken));
		char aNormalized[16] = "";
		NormalizeLanguageCode(aToken, aNormalized, sizeof(aNormalized));
		if(aNormalized[0] && str_comp(aLanguage, aNormalized) == 0)
			return true;
		pToken = pEnd;
	}
	return false;
}

bool CTranslate::ShouldTranslateOutgoingChat(const char *pText) const
{
	if(!g_Config.m_TcTranslateAutoOutgoing || !pText || !pText[0])
		return false;
	if(pText[0] == '/')
	{
		char aPrefix[MAX_CHAT_LENGTH] = "";
		char aBody[MAX_CHAT_LENGTH] = "";
		if(!ExtractOutgoingTranslatableText(*GameClient(), pText, aPrefix, sizeof(aPrefix), aBody, sizeof(aBody)))
			return false;
	}
	return !IsAutoLanguage(OutgoingTargetLanguage()) && !LanguagesEqual(OutgoingSourceLanguage(), OutgoingTargetLanguage());
}

bool CTranslate::CanStartRequest() const
{
	if(time() < m_NextRequestTime)
		return false;
	int ActiveRequests = 0;
	for(const CTranslateJob &Job : m_vJobs)
		ActiveRequests += Job.m_pBackend != nullptr;
	return ActiveRequests < 2;
}

void CTranslate::TranslateLine(CChat::CLine &Line, bool RespectIgnoredIncomingLanguages, bool Force, bool Manual)
{
	if(m_vJobs.size() >= 16 || (!Force && !CanStartRequest()))
		return;
	// The translate command detects the language itself, whatever the auto translation assumes.
	auto pBackend = CreateBackend(Line.m_aText, Manual ? "auto" : IncomingSourceLanguage(), IncomingTargetLanguage());
	if(!pBackend)
		return;

	CTranslateJob Job;
	Job.m_Type = CTranslateJob::EType::CHAT_LINE;
	Job.m_pLine = &Line;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_RespectIgnoredIncomingLanguages = RespectIgnoredIncomingLanguages;
	Job.m_Manual = Manual;
	Job.m_pBackend = std::move(pBackend);
	Job.m_Deadline = time() + time_freq() * TRANSLATE_TIMEOUT;
	Line.m_pTranslateResponse = Job.m_pTranslateResponse;
	m_NextRequestTime = time() + time_freq();
	m_vJobs.emplace_back(std::move(Job));
}

void CTranslate::OnConsoleInit()
{
	if(g_Config.m_TcTranslateAuto && !g_Config.m_TcTranslateAutoIncoming)
	{
		g_Config.m_TcTranslateAutoIncoming = 1;
		g_Config.m_TcTranslateAuto = 0;
	}
	if(g_Config.m_TcTranslateOutgoing && !g_Config.m_TcTranslateAutoOutgoing)
	{
		g_Config.m_TcTranslateAutoOutgoing = 1;
		g_Config.m_TcTranslateOutgoing = 0;
	}
	if(g_Config.m_ScTranslateOutgoingTarget[0] == '\0' || (str_comp(g_Config.m_ScTranslateOutgoingTarget, "en") == 0 && str_comp(g_Config.m_TcTranslateOutgoingTarget, "en") != 0))
		str_copy(g_Config.m_ScTranslateOutgoingTarget, g_Config.m_TcTranslateOutgoingTarget);
	Console()->Register("translate", "?r[count|player name]", CFGFLAG_CLIENT, ConTranslate, this, "Translate the last chat messages of players (a count up to 10), or a player's last messages (name, then an optional count)");
	Console()->Register("translate_id", "i[id] ?i[count]", CFGFLAG_CLIENT, ConTranslateId, this, "Translate the last chat message of the player with this client id");
	Console()->Register("translate_to_language", "s[TR|EN]", CFGFLAG_CLIENT, ConTranslateToLanguage, this, "Language the translate command translates into");
}

int CTranslate::TranslateRecent(int Count, const char *pName, int ClientId, std::string *pMatchedName)
{
	CChat &Chat = GameClient()->m_Chat;
	auto &&LineAt = [&](int i) -> CChat::CLine & {
		return Chat.m_aLines[((Chat.m_CurrentLine - i) % CChat::MAX_LINES + CChat::MAX_LINES) % CChat::MAX_LINES];
	};
	auto &&IsPlayerLine = [&](const CChat::CLine &Line) {
		if(!Line.m_Initialized || Line.m_ClientId < 0 || Line.m_aName[0] == '\0' || Line.m_aText[0] == '\0')
			return false;
		for(const int LocalId : GameClient()->m_aLocalIds)
			if(LocalId >= 0 && LocalId == Line.m_ClientId)
				return false;
		return true;
	};
	// A name matches exactly, or as the start of a name when nobody has it exactly ("rai" for "rain cakir").
	bool Prefix = false;
	if(pName && pName[0])
	{
		bool Exact = false;
		for(int i = 0; i < CChat::MAX_LINES && !Exact; i++)
			Exact = IsPlayerLine(LineAt(i)) && str_comp_nocase(LineAt(i).m_aName, pName) == 0;
		Prefix = !Exact;
	}
	int Started = 0;
	for(int i = 0; i < CChat::MAX_LINES && Started < Count; i++)
	{
		CChat::CLine &Line = LineAt(i);
		if(!IsPlayerLine(Line))
			continue;
		if(pName && pName[0] && (Prefix ? !str_startswith_nocase(Line.m_aName, pName) : str_comp_nocase(Line.m_aName, pName) != 0))
			continue;
		if(ClientId >= 0 && Line.m_ClientId != ClientId)
			continue;
		if(pMatchedName && pMatchedName->empty())
			*pMatchedName = Line.m_aName;
		Started++;
		if(Line.m_pTranslateResponse && Line.m_pTranslateResponse->m_Text[0])
		{
			Line.m_pTranslateResponse->m_Shown = true;
			Line.m_Time = time();
			continue;
		}
		TranslateLine(Line, false, true, true);
	}
	if(Started > 0)
		Chat.RebuildChat();
	return Started;
}

void CTranslate::ConTranslate(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pSelf = static_cast<CTranslate *>(pUserData);
	char aArg[128];
	str_copy(aArg, pResult->NumArguments() > 0 ? pResult->GetString(0) : "");
	str_utf8_trim_right(aArg);
	int Count = 1;
	const char *pName = nullptr;
	if(aArg[0])
	{
		if(str_isallnum(aArg))
			Count = std::clamp(str_toint(aArg), 1, 10);
		else
		{
			// "translate rain cakir 3": the player's last 3 messages; without a number their last 5.
			Count = 5;
			bool WholeIsName = false;
			for(const auto &Client : pSelf->GameClient()->m_aClients)
				WholeIsName |= Client.m_Active && str_comp_nocase(Client.m_aName, aArg) == 0;
			char *pLastSpace = (char *)str_rchr(aArg, ' ');
			if(!WholeIsName && pLastSpace && pLastSpace[1] && str_isallnum(pLastSpace + 1))
			{
				Count = std::clamp(str_toint(pLastSpace + 1), 1, 10);
				*pLastSpace = '\0';
			}
			pName = aArg;
		}
	}
	std::string Matched;
	const int Translated = pSelf->TranslateRecent(Count, pName, -1, &Matched);
	if(Translated == 0)
		pSelf->GameClient()->m_Chat.Echo(pName ? Localize("No message from that player to translate") : Localize("No message to translate"));
	else
		pSelf->EchoTranslated(Translated, pName ? Matched : std::string());
}

void CTranslate::EchoTranslated(int Translated, const std::string &Name)
{
	char aBuf[256];
	if(!Name.empty())
	{
		if(Translated == 1)
			str_format(aBuf, sizeof(aBuf), Localize("Translated %s's last message"), Name.c_str());
		else
			str_format(aBuf, sizeof(aBuf), Localize("Translated %s's last %d messages"), Name.c_str(), Translated);
	}
	else if(Translated == 1)
		str_copy(aBuf, Localize("Translated the last message"));
	else
		str_format(aBuf, sizeof(aBuf), Localize("Translated the last %d messages"), Translated);
	GameClient()->m_Chat.Echo(aBuf);
}

void CTranslate::ConTranslateId(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pSelf = static_cast<CTranslate *>(pUserData);
	const int Count = pResult->NumArguments() > 1 ? std::clamp(pResult->GetInteger(1), 1, 10) : 1;
	std::string Matched;
	const int Translated = pSelf->TranslateRecent(Count, nullptr, pResult->GetInteger(0), &Matched);
	if(Translated == 0)
		pSelf->GameClient()->m_Chat.Echo(Localize("No message from that player to translate"));
	else
		pSelf->EchoTranslated(Translated, Matched);
}

void CTranslate::ConTranslateToLanguage(IConsole::IResult *pResult, void *pUserData)
{
	CTranslate *pSelf = static_cast<CTranslate *>(pUserData);
	const char *pLanguage = pResult->GetString(0);
	const char *pCode = str_comp_nocase(pLanguage, "tr") == 0 ? "tr" : (str_comp_nocase(pLanguage, "en") == 0 ? "en" : nullptr);
	if(!pCode)
	{
		pSelf->GameClient()->m_Chat.Echo(Localize("Use translate_to_language TR or translate_to_language EN"));
		return;
	}
	str_copy(g_Config.m_TcTranslateTarget, pCode);
	pSelf->GameClient()->m_Chat.Echo(str_comp(pCode, "tr") == 0 ? "Messages are translated into Turkish" : "Messages are translated into English");
}

void CTranslate::AutoTranslate(CChat::CLine &Line)
{
	if(!g_Config.m_TcTranslateAutoIncoming || Line.m_ClientId < 0 || Line.m_aName[0] == '\0')
		return;
	for(const int LocalId : GameClient()->m_aLocalIds)
		if(LocalId >= 0 && LocalId == Line.m_ClientId)
			return;
	if(LanguagesEqual(IncomingSourceLanguage(), IncomingTargetLanguage()))
		return;
	TranslateLine(Line, true);
}

void CTranslate::TranslateMessage(CChat::CLine &Line)
{
	if(Line.m_ClientId < 0 || Line.m_aName[0] == '\0' || Line.m_aText[0] == '\0')
		return;
	for(const int LocalId : GameClient()->m_aLocalIds)
		if(LocalId >= 0 && LocalId == Line.m_ClientId)
			return;
	if(Line.m_pTranslateResponse && Line.m_pTranslateResponse->m_Text[0])
	{
		Line.m_pTranslateResponse->m_Shown = !Line.m_pTranslateResponse->m_Shown;
		Line.m_aYOffset[0] = -1.0f;
		Line.m_aYOffset[1] = -1.0f;
		GameClient()->m_Chat.RebuildChat();
		return;
	}
	if(LanguagesEqual(IncomingSourceLanguage(), IncomingTargetLanguage()))
		return;
	TranslateLine(Line, true);
}

bool CTranslate::ChatDoTranslateOutgoing(int Team, const char *pText)
{
	if(!ShouldTranslateOutgoingChat(pText))
		return false;
	if(!CanStartRequest())
	{
		GameClient()->m_Chat.Echo(Localize("Translation queue is busy; message was not sent"));
		return true;
	}
	if(m_vJobs.size() >= 16)
	{
		GameClient()->m_Chat.Echo(Localize("Translation queue is full"));
		return true;
	}

	char aPrefix[MAX_CHAT_LENGTH] = "";
	char aBody[MAX_CHAT_LENGTH] = "";
	if(!ExtractOutgoingTranslatableText(*GameClient(), pText, aPrefix, sizeof(aPrefix), aBody, sizeof(aBody)))
		return false;

	auto pBackend = CreateBackend(aBody, OutgoingSourceLanguage(), OutgoingTargetLanguage());
	if(!pBackend)
	{
		GameClient()->m_Chat.Echo(Localize("Invalid translate backend"));
		return true;
	}

	CTranslateJob Job;
	Job.m_Type = CTranslateJob::EType::OUTGOING_CHAT;
	Job.m_Team = Team;
	Job.m_pTranslateResponse = std::make_shared<CTranslateResponse>();
	Job.m_pBackend = std::move(pBackend);
	Job.m_Deadline = time() + time_freq() * TRANSLATE_TIMEOUT;
	str_copy(Job.m_aTextToTranslate, aBody);
	str_copy(Job.m_aOutgoingPrefix, aPrefix);
	m_NextRequestTime = time() + time_freq();
	m_vJobs.emplace_back(std::move(Job));
	return true;
}

void CTranslate::ToggleShiftClick()
{
	const int Target = std::clamp(g_Config.m_ScTranslateShiftClickTarget, 0, 2);
	if(Target == 0)
		g_Config.m_TcTranslateAutoIncoming ^= 1;
	else if(Target == 1)
		g_Config.m_TcTranslateAutoOutgoing ^= 1;
	else if(g_Config.m_TcTranslateAutoIncoming && g_Config.m_TcTranslateAutoOutgoing)
	{
		g_Config.m_TcTranslateAutoIncoming = 0;
		g_Config.m_TcTranslateAutoOutgoing = 0;
	}
	else
	{
		g_Config.m_TcTranslateAutoIncoming = 1;
		g_Config.m_TcTranslateAutoOutgoing = 1;
	}
}

void CTranslate::OnRender()
{
	const int64_t Now = time();
	auto ForEach = [&](CTranslateJob &Job) {
		const bool TimedOut = Now >= Job.m_Deadline;
		if(Job.m_Type == CTranslateJob::EType::OUTGOING_CHAT)
		{
			if(TimedOut)
			{
				GameClient()->m_Chat.Echo(Localize("Translation timed out; message was not sent"));
				return true;
			}
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false;
			if(!*Done || !Job.m_pTranslateResponse->m_Text[0])
			{
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text) + 96];
				str_format(aBuf, sizeof(aBuf), Localize("Translation failed; message was not sent: %s"), Job.m_pTranslateResponse->m_Text);
				GameClient()->m_Chat.Echo(aBuf);
				return true;
			}
			if(g_Config.m_ScTranslateOutgoingStripPunctuation)
				StripApostrophesFromTranslatedText(Job.m_pTranslateResponse->m_Text);
			char aText[MAX_CHAT_LENGTH] = "";
			if(Job.m_aOutgoingPrefix[0])
			{
				str_copy(aText, Job.m_aOutgoingPrefix, sizeof(aText));
				str_append(aText, Job.m_pTranslateResponse->m_Text, sizeof(aText));
			}
			else
				str_copy(aText, Job.m_pTranslateResponse->m_Text, sizeof(aText));
			if(aText[0])
				GameClient()->m_Chat.SendTranslatedChatQueued(Job.m_Team, aText);
			else
				GameClient()->m_Chat.Echo(Localize("Translation failed; message was not sent"));
			return true;
		}

		if(!Job.m_pLine || Job.m_pLine->m_pTranslateResponse != Job.m_pTranslateResponse)
			return true;
		if(TimedOut)
		{
			str_copy(Job.m_pTranslateResponse->m_Text, Localize("Translation timed out"));
			Job.m_pTranslateResponse->m_Error = true;
		}
		else
		{
			const std::optional<bool> Done = Job.m_pBackend->Update(*Job.m_pTranslateResponse);
			if(!Done.has_value())
				return false;
			if(*Done)
			{
				if(Job.m_RespectIgnoredIncomingLanguages && IsIgnoredIncomingLanguage(Job.m_pTranslateResponse->m_Language))
					Job.m_pTranslateResponse->m_Text[0] = '\0';
				if(str_comp_nocase(Job.m_pLine->m_aText, Job.m_pTranslateResponse->m_Text) == 0)
				{
					Job.m_pTranslateResponse->m_Text[0] = '\0';
					if(Job.m_Manual)
						m_ManualAlreadyInTarget++;
				}
			}
			else
			{
				char aBuf[sizeof(Job.m_pTranslateResponse->m_Text)];
				str_format(aBuf, sizeof(aBuf), Localize("Translation failed: %s"), Job.m_pTranslateResponse->m_Text);
				Job.m_pTranslateResponse->m_Error = true;
				str_copy(Job.m_pTranslateResponse->m_Text, aBuf);
			}
		}
		// The chat box shows the translation under the line; the console gets it too for the command.
		if(Job.m_Manual && Job.m_pTranslateResponse->m_Text[0])
			log_info("translate", "%s: %s", Job.m_pLine->m_aName, Job.m_pTranslateResponse->m_Text);
		Job.m_pLine->m_Time = Now;
		GameClient()->m_Chat.RebuildChat();
		return true;
	};
	m_vJobs.erase(std::remove_if(m_vJobs.begin(), m_vJobs.end(), ForEach), m_vJobs.end());

	// Once the command's lines are done, say why nothing showed up for some of them.
	if(m_ManualAlreadyInTarget > 0 && std::none_of(m_vJobs.begin(), m_vJobs.end(), [](const CTranslateJob &Job) { return Job.m_Manual; }))
	{
		char aBuf[128];
		if(m_ManualAlreadyInTarget == 1)
			str_format(aBuf, sizeof(aBuf), Localize("That message is already in %s"), str_comp_nocase(IncomingTargetLanguage(), "tr") == 0 ? "Turkish" : (str_comp_nocase(IncomingTargetLanguage(), "en") == 0 ? "English" : IncomingTargetLanguage()));
		else
			str_format(aBuf, sizeof(aBuf), Localize("%d messages are already in %s"), m_ManualAlreadyInTarget, str_comp_nocase(IncomingTargetLanguage(), "tr") == 0 ? "Turkish" : (str_comp_nocase(IncomingTargetLanguage(), "en") == 0 ? "English" : IncomingTargetLanguage()));
		GameClient()->m_Chat.Echo(aBuf);
		m_ManualAlreadyInTarget = 0;
	}
}
