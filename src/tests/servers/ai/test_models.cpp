/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include <Application.h>
#include <DataIO.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <Json.h>
#include <Message.h>
#include <String.h>
#include <Url.h>
#include <UrlProtocolListener.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <os/ai/AIConfig.h>

using namespace BPrivate::Network;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

class SyncListener : public BUrlProtocolListener {
public:
	virtual void RequestCompleted(BUrlRequest* caller, bool success) override {}
};

static void
PrintUsage(const char* progName)
{
	printf("Uso: %s [opzioni]\n", progName);
	printf("Opzioni:\n");
	printf("  -h, --help           Mostra questa guida\n");
	printf("  -l, --list           Elenca solo i modelli disponibili senza testarli\n");
	printf("  -m, --model <nome>   Testa solo il modello specificato\n");
	printf("  -p, --plugin <nome>  Specifica il plugin (default: da AISettings o OpenAIPlugin)\n");
	printf("  -u, --url <url>      Specifica il base URL (default: da AISettings)\n");
	printf("  -k, --key <chiave>   Specifica la chiave API (default: dal KeyStore)\n");
	printf("  --prompt <testo>     Prompt di test da inviare (default: \"ping\")\n");
	printf("  --all                Testa tutti i modelli trovati sul server\n");
}

static status_t
PerformHttpRequest(const BString& urlStr, const char* method, const BString& apiKey,
	const BString* postBody, BString& outResponse, int32& outStatusCode)
{
	BUrl url(urlStr.String(), true);
	BMallocIO outBuffer;
	SyncListener listener;

	BUrlRequest* req = BUrlProtocolRoster::MakeRequest(url, &outBuffer, &listener, NULL);
	if (!req)
		return B_ERROR;

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
	if (http) {
		http->SetMethod(method);
		BHttpHeaders headers;
		headers.AddHeader("Content-Type", "application/json");

		if (!apiKey.IsEmpty()) {
			BString authHeader;
			authHeader << "Bearer " << apiKey;
			headers.AddHeader("Authorization", authHeader.String());
		}
		http->SetHeaders(headers);

		if (postBody != nullptr && postBody->Length() > 0) {
			BMallocIO* input = new (std::nothrow) BMallocIO();
			if (input) {
				input->WriteExactly(postBody->String(), postBody->Length());
				http->AdoptInputData(input, postBody->Length());
			}
		}
	}

	thread_id thread = req->Run();
	status_t rc = B_ERROR;
	if (thread >= 0) {
		wait_for_thread(thread, &rc);
	} else {
		rc = thread;
	}

	outStatusCode = 0;
	if (rc == B_OK && http != nullptr) {
		const BHttpResult* result = dynamic_cast<const BHttpResult*>(&http->Result());
		if (result != nullptr)
			outStatusCode = result->StatusCode();
	}

	outResponse.SetTo((const char*)outBuffer.Buffer(), outBuffer.BufferLength());
	delete req;
	return rc;
}

static void
ExtractModelIds(const BString& jsonResponse, std::vector<BString>& outModels)
{
	int32 currentPos = 0;
	while (true) {
		int32 matchPos = jsonResponse.FindFirst("\"id\": \"", currentPos);
		if (matchPos == B_ERROR)
			matchPos = jsonResponse.FindFirst("\"id\":\"", currentPos);
		if (matchPos == B_ERROR)
			break;

		matchPos = jsonResponse.FindFirst("\"", matchPos + 4) + 1;
		int32 endPos = jsonResponse.FindFirst("\"", matchPos);
		if (endPos == B_ERROR)
			break;

		BString modelName;
		jsonResponse.CopyInto(modelName, matchPos, endPos - matchPos);

		bool isEmbedding = (modelName.FindFirst("embedding") != B_ERROR);
		bool isWhisper = (modelName.FindFirst("whisper") != B_ERROR);
		bool isTTS = (modelName.FindFirst("tts") != B_ERROR);
		bool isModeration = (modelName.FindFirst("moderation") != B_ERROR);

		if (!isEmbedding && !isWhisper && !isTTS && !isModeration) {
			outModels.push_back(modelName);
		}
		currentPos = endPos + 1;
	}
}

static void
ExtractErrorDetails(const BString& jsonResponse, BString& outError)
{
	BMessage parsed;
	BMessage errorObj;
	if (BJson::Parse(jsonResponse.String(), parsed) == B_OK
		&& parsed.FindMessage("error", &errorObj) == B_OK) {
		const char* msg = nullptr;
		if (errorObj.FindString("message", &msg) == B_OK && msg) {
			outError = msg;
			return;
		}
	}
	outError = jsonResponse;
	outError.Trim();
}

static void
ExtractAssistantReply(const BString& jsonResponse, BString& outReply)
{
	BMessage parsed;
	if (BJson::Parse(jsonResponse.String(), parsed) == B_OK) {
		BMessage choices, choiceZero, message;
		if (parsed.FindMessage("choices", &choices) == B_OK
			&& (choices.FindMessage("0", &choiceZero) == B_OK
				|| choices.FindMessage("msg", 0, &choiceZero) == B_OK)
			&& choiceZero.FindMessage("message", &message) == B_OK) {
			const char* content = nullptr;
			const char* reasoning = nullptr;
			message.FindString("content", &content);
			message.FindString("reasoning_content", &reasoning);

			if (content && content[0] != '\0') {
				outReply = content;
				outReply.Trim();
				return;
			}
			if (reasoning && reasoning[0] != '\0') {
				outReply = "[reasoning] ";
				outReply << reasoning;
				outReply.Trim();
				return;
			}
		}
	}
	outReply = jsonResponse;
	outReply.Trim();
}

int main(int argc, char** argv)
{
	BApplication app("application/x-vnd.AI-TestModels");

	BString pluginName = "OpenAIPlugin";
	BString baseUrl = "";
	BString apiKey = "";
	BString targetModel = "";
	BString testPrompt = "ping";
	bool listOnly = false;
	bool testAll = false;

	// 1. Carica impostazioni globali di Haiku
	AISettings settings;
	if (LoadAISettings(settings)) {
		if (!settings.plugin.IsEmpty())
			pluginName = settings.plugin;
		if (!settings.base_url.IsEmpty())
			baseUrl = settings.base_url;
		if (!settings.model.IsEmpty())
			targetModel = settings.model;
	}

	// 2. Parsa argomenti da riga di comando
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			PrintUsage(argv[0]);
			return 0;
		} else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
			listOnly = true;
		} else if (strcmp(argv[i], "--all") == 0) {
			testAll = true;
		} else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0) && i + 1 < argc) {
			targetModel = argv[++i];
		} else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--plugin") == 0) && i + 1 < argc) {
			pluginName = argv[++i];
		} else if ((strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--url") == 0) && i + 1 < argc) {
			baseUrl = argv[++i];
		} else if ((strcmp(argv[i], "-k") == 0 || strcmp(argv[i], "--key") == 0) && i + 1 < argc) {
			apiKey = argv[++i];
		} else if (strcmp(argv[i], "--prompt") == 0 && i + 1 < argc) {
			testPrompt = argv[++i];
		} else {
			// Se e' un argomento posizionale, trattalo come targetModel
			targetModel = argv[i];
		}
	}

	// 3. Se apiKey non specificata, recuperala dal KeyStore
	if (apiKey.IsEmpty()) {
		GetPluginAPIKey(pluginName.String(), apiKey);
	}

	if (baseUrl.IsEmpty()) {
		baseUrl = "https://api.openai.com";
	}

	printf("============================================================\n");
	printf("   AI TEST MODELS - Haiku OS\n");
	printf("============================================================\n");
	printf("Plugin:     %s\n", pluginName.String());
	printf("Base URL:   %s\n", baseUrl.String());
	printf("API Key:    %s (lunghezza: %d byte)\n",
		apiKey.IsEmpty() ? "(nessuna)" : "presente", (int)apiKey.Length());
	printf("Modello default: %s\n", targetModel.IsEmpty() ? "(nessuno)" : targetModel.String());
	printf("============================================================\n\n");

	// Costruzione URL per /v1/models
	BString modelsUrl = baseUrl;
	if (modelsUrl.EndsWith("/"))
		modelsUrl.Remove(modelsUrl.Length() - 1, 1);
	if (!modelsUrl.EndsWith("/v1") && !modelsUrl.EndsWith("/models"))
		modelsUrl << "/v1/models";
	else if (!modelsUrl.EndsWith("/models"))
		modelsUrl << "/models";

	printf("[1/2] Recupero lista modelli da: %s ...\n", modelsUrl.String());
	BString modelsResponse;
	int32 statusCode = 0;
	status_t rc = PerformHttpRequest(modelsUrl, B_HTTP_GET, apiKey, nullptr, modelsResponse, statusCode);

	std::vector<BString> availableModels;
	if (rc == B_OK && statusCode == 200) {
		ExtractModelIds(modelsResponse, availableModels);
		printf("-> Trovati %d modelli disponibili:\n", (int)availableModels.size());
		for (size_t i = 0; i < availableModels.size(); i++) {
			printf("   [%zu] %s\n", i + 1, availableModels[i].String());
		}
	} else {
		printf("-> ERRORE nel recupero dei modelli! (Status HTTP: %" B_PRId32 ", RC: %" B_PRId32 ")\n", statusCode, rc);
		BString err;
		ExtractErrorDetails(modelsResponse, err);
		printf("   Dettaglio: %s\n\n", err.String());
		if (!targetModel.IsEmpty()) {
			printf("Aggiungo comunque il modello target \"%s\" alla lista di test.\n", targetModel.String());
			availableModels.push_back(targetModel);
		}
	}

	if (listOnly) {
		printf("\nCompletato. (Opzione --list specificata)\n");
		return 0;
	}

	// Costruzione URL per /v1/chat/completions
	BString chatUrl = baseUrl;
	if (chatUrl.EndsWith("/"))
		chatUrl.Remove(chatUrl.Length() - 1, 1);
	if (!chatUrl.EndsWith("/v1/chat/completions"))
		chatUrl << "/v1/chat/completions";

	// Determina i modelli da testare
	std::vector<BString> modelsToTest;
	if (testAll) {
		modelsToTest = availableModels;
	} else if (!targetModel.IsEmpty()) {
		modelsToTest.push_back(targetModel);
	} else if (!availableModels.empty()) {
		modelsToTest.push_back(availableModels[0]);
	}

	if (modelsToTest.empty()) {
		printf("\nNessun modello da testare. Usa -m <nome_modello> o --all\n");
		return 1;
	}

	printf("\n[2/2] Test chat completions su: %s\n", chatUrl.String());
	printf("      Prompt: \"%s\"\n", testPrompt.String());
	printf("------------------------------------------------------------\n");

	struct TestResult {
		BString model;
		int32 statusCode;
		bool success;
		BString details;
	};
	std::vector<TestResult> results;

	for (size_t i = 0; i < modelsToTest.size(); i++) {
		const BString& m = modelsToTest[i];
		printf("\nTesting [%zu/%zu]: %s ...\n", i + 1, modelsToTest.size(), m.String());

		BString payload;
		payload << "{\n"
				<< "  \"model\": \"" << m << "\",\n"
				<< "  \"messages\": [\n"
				<< "    {\"role\": \"user\", \"content\": \"" << testPrompt << "\"}\n"
				<< "  ],\n"
				<< "  \"max_tokens\": 15\n"
				<< "}";

		BString response;
		int32 code = 0;
		status_t reqRc = PerformHttpRequest(chatUrl, B_HTTP_POST, apiKey, &payload, response, code);

		TestResult res;
		res.model = m;
		res.statusCode = code;
		res.success = (reqRc == B_OK && code == 200);

		if (res.success) {
			BString reply;
			ExtractAssistantReply(response, reply);
			res.details = reply;
			printf("  [OK] Risposta: %s\n", reply.String());
		} else {
			BString err;
			ExtractErrorDetails(response, err);
			res.details = err;
			printf("  [FALLITO - HTTP %" B_PRId32 "] Errore: %s\n", code, err.String());
		}
		results.push_back(res);
	}

	printf("\n============================================================\n");
	printf("   RIASSUNTO RISULTATI\n");
	printf("============================================================\n");
	int passed = 0;
	for (const auto& r : results) {
		if (r.success) {
			passed++;
			printf("  [V] %-45s -> OK (HTTP 200)\n", r.model.String());
		} else {
			printf("  [X] %-45s -> FALLITO (HTTP %" B_PRId32 ")\n", r.model.String(), r.statusCode);
		}
	}
	printf("------------------------------------------------------------\n");
	printf("Totale: %d/%zu modelli funzionanti.\n", passed, results.size());
	printf("============================================================\n");

	return (passed > 0) ? 0 : 1;
}
