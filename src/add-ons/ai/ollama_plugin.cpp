// ollama_plugin.cpp
// Plugin Ollama per Haiku AI con supporto sync + streaming NDJSON.

#include <os/ai/AIPlugin.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <UrlProtocolListener.h>
#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <String.h>
#include <File.h>
#include <OS.h>

#include <Json.h>
#include <HttpResult.h>

using namespace BPrivate::Network;

#define DEFAULT_OLLAMA_URL   "http://localhost:11434"
#define DEFAULT_OLLAMA_MODEL "llama3"

struct OllamaHandle {
	char* base_url;
};

struct OllamaAsyncArgs {
	char* model;
	char* notify_path;
	char* base_url;
	BMessage* context_copy;
};

static char*
dupstr_or_null(const char* s)
{
	if (!s)
		return NULL;

	size_t length = strlen(s) + 1;
	char* copy = (char*)malloc(length);
	if (copy != NULL)
		memcpy(copy, s, length);
	return copy;
}


static void
AppendEscapedJsonString(BString& out, const char* text)
{
	if (text == NULL)
		return;

	for (const char* p = text; *p != '\0'; ++p) {
		switch (*p) {
			case '\\':
				out.Append("\\\\");
				break;
			case '"':
				out.Append("\\\"");
				break;
			case '\n':
				out.Append("\\n");
				break;
			case '\r':
				out.Append("\\r");
				break;
			case '\t':
				out.Append("\\t");
				break;
			default:
				out.Append(p, 1);
				break;
		}
	}
}


static status_t
CopyToBuffer(const char* text, char* out, size_t outLen)
{
	if (out == NULL || outLen == 0)
		return B_BAD_VALUE;

	if (text == NULL)
		text = "";

	size_t length = strlen(text);
	size_t copyLength = length < outLen - 1 ? length : outLen - 1;
	memcpy(out, text, copyLength);
	out[copyLength] = '\0';
	return B_OK;
}


static void
WriteStreamEnd(const char* notifyPath)
{
	if (notifyPath == NULL || notifyPath[0] == '\0')
		return;

	BFile file(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
	if (file.InitCheck() != B_OK)
		return;

	static const char* kEndMarker = "<<STREAM_END>>";
	file.Write(kEndMarker, strlen(kEndMarker));
}


static void
FreeAsyncArgs(OllamaAsyncArgs* args)
{
	if (args == NULL)
		return;

	if (args->model != NULL)
		free(args->model);
	if (args->notify_path != NULL)
		free(args->notify_path);
	if (args->base_url != NULL)
		free(args->base_url);
	delete args->context_copy;
	free(args);
}


static BString
BuildChatUrl(const char* baseUrl)
{
	BString url = (baseUrl != NULL && baseUrl[0] != '\0') ? baseUrl : DEFAULT_OLLAMA_URL;
	if (!url.EndsWith("/"))
		url.Append("/");
	url.Append("api/chat");
	return url;
}


static BString
BuildTagsUrl(const char* baseUrl)
{
	BString url = (baseUrl != NULL && baseUrl[0] != '\0') ? baseUrl : DEFAULT_OLLAMA_URL;
	if (!url.EndsWith("/"))
		url.Append("/");
	url.Append("api/tags");
	return url;
}


static bool
ExtractJsonStringValue(const char* source, const char* marker, BString& out)
{
	if (source == NULL || marker == NULL)
		return false;

	const char* start = strstr(source, marker);
	if (start == NULL)
		return false;

	start += strlen(marker);
	out.Truncate(0);

	bool escaping = false;
	for (const char* p = start; *p != '\0'; ++p) {
		char ch = *p;
		if (escaping) {
			switch (ch) {
				case 'n':
					out.Append("\n");
					break;
				case 'r':
					out.Append("\r");
					break;
				case 't':
					out.Append("\t");
					break;
				case '"':
					out.Append("\"");
					break;
				case '\\':
					out.Append("\\");
					break;
				case '/':
					out.Append("/");
					break;
				case 'b':
					out.Append("\b");
					break;
				case 'f':
					out.Append("\f");
					break;
				default:
					out.Append(&ch, 1);
					break;
			}
			escaping = false;
			continue;
		}

		if (ch == '\\') {
			escaping = true;
			continue;
		}

		if (ch == '"')
			return true;

		out.Append(&ch, 1);
	}

	return false;
}


static bool
ExtractOllamaContent(const char* json, BString& out)
{
	if (json == NULL)
		return false;

	const char* messagePos = strstr(json, "\"message\"");
	if (messagePos != NULL) {
		if (ExtractJsonStringValue(messagePos, "\"content\":\"", out))
			return true;
		if (ExtractJsonStringValue(messagePos, "\"content\": \"", out))
			return true;
	}

	if (ExtractJsonStringValue(json, "\"content\":\"", out))
		return true;
	return ExtractJsonStringValue(json, "\"content\": \"", out);
}


static void
PrepareContextForStreaming(BMessage* context, const char* prompt, const char* model)
{
	if (context == NULL)
		return;

	if (model != NULL && model[0] != '\0') {
		context->RemoveName("model_name");
		context->AddString("model_name", model);
	}

	BMessage history;
	bool hasHistory = false;
	if (context->FindMessage("messages", &history) == B_OK) {
		BMessage turn;
		BString numericKey;
		if (history.FindMessage("msg", 0, &turn) == B_OK) {
			hasHistory = true;
		} else {
			numericKey.SetToFormat("%d", 0);
			hasHistory = history.FindMessage(numericKey.String(), &turn) == B_OK;
		}
	}

	if (!hasHistory && prompt != NULL && prompt[0] != '\0') {
		BMessage messages;
		BMessage turn;
		turn.AddString("role", "user");
		turn.AddString("content", prompt);
		messages.AddMessage("msg", &turn);
		context->RemoveName("messages");
		context->AddMessage("messages", &messages);
	}
}


static void
BuildPayloadFromContext(const BMessage* config, const char* currentPrompt,
	BString& outPayload, bool stream)
{
	const char* model = NULL;
	if (config != NULL)
		config->FindString("model_name", &model);
	if (model == NULL || model[0] == '\0')
		model = DEFAULT_OLLAMA_MODEL;

	outPayload.SetTo("{");
	outPayload << "\"model\":\"";
	AppendEscapedJsonString(outPayload, model);
	outPayload << "\",\"stream\":";
	outPayload << (stream ? "true" : "false");
	outPayload << ",\"messages\":[";

	bool first = true;
	BMessage messages;
	if (config != NULL && config->FindMessage("messages", &messages) == B_OK) {
		BMessage turn;
		int32 index = 0;
		while (messages.FindMessage("msg", index, &turn) == B_OK
			|| messages.FindMessage(BString().SetToFormat("%ld", (long)index).String(),
				&turn) == B_OK) {
			const char* role = NULL;
			const char* content = NULL;
			turn.FindString("role", &role);
			if (turn.FindString("content", &content) != B_OK)
				turn.FindString("text", &content);

			if (role != NULL && content != NULL && content[0] != '\0') {
				if (!first)
					outPayload.Append(",");

				BString mappedRole = role;
				if (mappedRole == "model")
					mappedRole = "assistant";

				outPayload << "{\"role\":\"";
				AppendEscapedJsonString(outPayload, mappedRole.String());
				outPayload << "\",\"content\":\"";
				AppendEscapedJsonString(outPayload, content);
				outPayload << "\"}";
				first = false;
			}
			index++;
		}
	}

	if (first && currentPrompt != NULL && currentPrompt[0] != '\0') {
		outPayload << "{\"role\":\"user\",\"content\":\"";
		AppendEscapedJsonString(outPayload, currentPrompt);
		outPayload << "\"}";
	}

	outPayload << "]}";
}


class SyncListener : public BUrlProtocolListener {
public:
	virtual bool CertificateVerificationFailed(BUrlRequest* request,
		BCertificate& certificate, const char* message)
	{
		(void)request;
		(void)certificate;
		(void)message;
		return false;
	}
};


class CompletionListener : public BUrlProtocolListener {
public:
	CompletionListener(const char* notifyPath)
		:
		fPath(notifyPath)
	{
	}

	virtual void RequestCompleted(BUrlRequest* request, bool success)
	{
		(void)request;
		(void)success;
		WriteStreamEnd(fPath.String());
	}

private:
	BString fPath;
};


class OllamaStreamTarget : public BDataIO {
public:
	OllamaStreamTarget(const char* notifyPath)
	{
		fFile.SetTo(notifyPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	}

	virtual ssize_t Write(const void* buffer, size_t size)
	{
		if (fFile.InitCheck() != B_OK || size == 0)
			return (ssize_t)size;

		fBuffer.Append((const char*)buffer, size);
		int32 processedPos = 0;

		while (true) {
			int32 nextNewline = fBuffer.FindFirst("\n", processedPos);
			if (nextNewline == B_ERROR)
				break;

			BString line;
			fBuffer.CopyInto(line, processedPos, nextNewline - processedPos);
			line.Trim();
			processedPos = nextNewline + 1;

			if (line.IsEmpty())
				continue;

			BString token;
			if (ExtractOllamaContent(line.String(), token) && token.Length() > 0)
				fFile.Write(token.String(), token.Length());
		}

		if (processedPos > 0)
			fBuffer.Remove(0, processedPos);

		return (ssize_t)size;
	}

private:
	BFile fFile;
	BString fBuffer;
};


static status_t
ollama_stream_thread_func(void* data)
{
	OllamaAsyncArgs* args = (OllamaAsyncArgs*)data;
	if (args == NULL)
		return B_BAD_VALUE;

	fprintf(stderr, "[OLLAMA PLUGIN] streaming worker avviato\n");

	if (args->notify_path == NULL || args->notify_path[0] == '\0') {
		fprintf(stderr, "[OLLAMA PLUGIN] notify_path mancante\n");
		FreeAsyncArgs(args);
		return B_BAD_VALUE;
	}

	BString url = BuildChatUrl(args->base_url);
	PrepareContextForStreaming(args->context_copy, NULL, args->model);

	BString payload;
	BuildPayloadFromContext(args->context_copy, NULL, payload, true);

	OllamaStreamTarget streamTarget(args->notify_path);
	CompletionListener listener(args->notify_path);
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(
		BUrl(url.String(), false), &streamTarget, &listener, NULL);
	if (request == NULL) {
		fprintf(stderr, "[OLLAMA PLUGIN] creazione request streaming fallita\n");
		WriteStreamEnd(args->notify_path);
		FreeAsyncArgs(args);
		return B_ERROR;
	}

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http != NULL) {
		http->SetMethod(B_HTTP_POST);

		BHttpHeaders headers;
		headers.AddHeader("Content-Type", "application/json");
		http->SetHeaders(headers);

		BMemoryIO* input = new BMemoryIO(payload.String(), payload.Length());
		http->AdoptInputData(input, payload.Length());
	}

	thread_id thread = request->Run();
	if (thread >= 0) {
		status_t rc = B_ERROR;
		wait_for_thread(thread, &rc);
		fprintf(stderr, "[OLLAMA PLUGIN] streaming terminato rc=%ld\n", (long)rc);
	} else {
		fprintf(stderr, "[OLLAMA PLUGIN] avvio thread streaming fallito\n");
		WriteStreamEnd(args->notify_path);
	}

	delete request;
	FreeAsyncArgs(args);
	return B_OK;
}


extern "C" ai_plugin_t
ai_plugin_init(const BMessage* settingsMsg)
{
	OllamaHandle* handle = (OllamaHandle*)malloc(sizeof(OllamaHandle));
	if (handle == NULL)
		return NULL;

	handle->base_url = NULL;
	if (settingsMsg != NULL) {
		const char* baseUrl = NULL;
		if (settingsMsg->FindString("base_url", &baseUrl) == B_OK
			&& baseUrl != NULL && baseUrl[0] != '\0') {
			handle->base_url = dupstr_or_null(baseUrl);
		}
	}

	return (ai_plugin_t)handle;
}


extern "C" void
ai_plugin_free(ai_plugin_t handle)
{
	OllamaHandle* typedHandle = (OllamaHandle*)handle;
	if (typedHandle == NULL)
		return;

	if (typedHandle->base_url != NULL)
		free(typedHandle->base_url);
	free(typedHandle);
}


extern "C" uint32
ai_plugin_get_capabilities(void)
{
	return AI_CAP_STREAMING;
}


extern "C" status_t
ai_plugin_generate_text_sync(ai_plugin_t handle, const char* prompt,
	char* response_buf, size_t response_len, BMessage* contextMsg)
{
	if (response_buf == NULL || response_len == 0)
		return B_BAD_VALUE;

	OllamaHandle* typedHandle = (OllamaHandle*)handle;
	BString url = BuildChatUrl(typedHandle != NULL ? typedHandle->base_url : NULL);

	BString payload;
	BuildPayloadFromContext(contextMsg, prompt, payload, false);
	fprintf(stderr, "[OLLAMA PLUGIN] sync request verso %s\n", url.String());

	BMallocIO* output = new BMallocIO();
	SyncListener listener;
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(
		BUrl(url.String(), false), output, &listener, NULL);
	if (request == NULL) {
		delete output;
		CopyToBuffer("{\"error\":\"request failed\"}", response_buf, response_len);
		return B_ERROR;
	}

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http != NULL) {
		http->SetMethod(B_HTTP_POST);

		BHttpHeaders headers;
		headers.AddHeader("Content-Type", "application/json");
		http->SetHeaders(headers);

		BMallocIO* input = new BMallocIO();
		input->WriteExactly(payload.String(), payload.Length());
		http->AdoptInputData(input, payload.Length());
	}

	thread_id thread = request->Run();
	status_t rc = B_ERROR;
	if (thread >= 0)
		wait_for_thread(thread, &rc);
	else
		rc = thread;

	int32 statusCode = 0;
	if (http != NULL) {
		const BHttpResult* httpResult
			= dynamic_cast<const BHttpResult*>(&(http->Result()));
		if (httpResult != NULL)
			statusCode = httpResult->StatusCode();
	}

	const void* buffer = output->Buffer();
	size_t length = output->BufferLength();
	BString rawResponse;
	if (buffer != NULL && length > 0)
		rawResponse.SetTo((const char*)buffer, length);

	BString extracted;
	if (statusCode == 200 && ExtractOllamaContent(rawResponse.String(), extracted)) {
		CopyToBuffer(extracted.String(), response_buf, response_len);
	} else if (length > 0) {
		CopyToBuffer(rawResponse.String(), response_buf, response_len);
	} else {
		CopyToBuffer("{\"error\":\"empty response\"}", response_buf, response_len);
	}

	delete request;
	delete output;

	return (rc == B_OK && statusCode == 200) ? B_OK : B_ERROR;
}


extern "C" status_t
ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt,
	BMessage* contextMsg)
{
	OllamaHandle* typedHandle = (OllamaHandle*)handle;
	if (contextMsg == NULL)
		return B_BAD_VALUE;

	const char* notifyPath = NULL;
	const char* model = NULL;
	contextMsg->FindString("notify_path", &notifyPath);
	contextMsg->FindString("model_name", &model);

	if (notifyPath == NULL || notifyPath[0] == '\0')
		return B_BAD_VALUE;

	OllamaAsyncArgs* args = (OllamaAsyncArgs*)malloc(sizeof(OllamaAsyncArgs));
	if (args == NULL)
		return B_NO_MEMORY;

	args->model = dupstr_or_null(
		(model != NULL && model[0] != '\0') ? model : DEFAULT_OLLAMA_MODEL);
	args->notify_path = dupstr_or_null(notifyPath);
	args->base_url = typedHandle != NULL ? dupstr_or_null(typedHandle->base_url) : NULL;
	args->context_copy = new BMessage(*contextMsg);
	if (args->context_copy == NULL) {
		FreeAsyncArgs(args);
		return B_NO_MEMORY;
	}

	PrepareContextForStreaming(args->context_copy, prompt, args->model);

	thread_id thread = spawn_thread(ollama_stream_thread_func,
		"ollama_stream_worker", B_NORMAL_PRIORITY, args);
	if (thread < B_OK) {
		FreeAsyncArgs(args);
		return B_ERROR;
	}

	resume_thread(thread);
	return B_OK;
}


extern "C" status_t
ai_plugin_list_models(const BMessage* settingsMsg, char* out_buf, size_t out_len)
{
	static const char* kFallbackModels = "[\"llama3\",\"llama3.2\",\"mistral\"]";
	if (out_buf == NULL || out_len == 0)
		return B_BAD_VALUE;

	const char* baseUrl = NULL;
	if (settingsMsg != NULL)
		settingsMsg->FindString("base_url", &baseUrl);

	BString url = BuildTagsUrl(baseUrl);
	BMallocIO* output = new BMallocIO();
	SyncListener listener;
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(
		BUrl(url.String(), false), output, &listener, NULL);
	if (request == NULL) {
		delete output;
		return CopyToBuffer(kFallbackModels, out_buf, out_len);
	}

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http != NULL)
		http->SetMethod(B_HTTP_GET);

	thread_id thread = request->Run();
	status_t rc = B_ERROR;
	if (thread >= 0)
		wait_for_thread(thread, &rc);
	else
		rc = thread;

	int32 statusCode = 0;
	if (http != NULL) {
		const BHttpResult* httpResult
			= dynamic_cast<const BHttpResult*>(&(http->Result()));
		if (httpResult != NULL)
			statusCode = httpResult->StatusCode();
	}

	BString result("[");
	bool first = true;
	if (rc == B_OK && statusCode == 200 && output->BufferLength() > 0) {
		BString response((const char*)output->Buffer(), output->BufferLength());
		int32 currentPos = 0;
		while (true) {
			int32 matchPos = response.FindFirst("\"name\":\"", currentPos);
			int32 advance = 8;
			if (matchPos == B_ERROR) {
				matchPos = response.FindFirst("\"name\": \"", currentPos);
				advance = 9;
			}
			if (matchPos == B_ERROR)
				break;

			matchPos += advance;
			int32 endPos = response.FindFirst("\"", matchPos);
			if (endPos == B_ERROR)
				break;

			BString modelName;
			response.CopyInto(modelName, matchPos, endPos - matchPos);
			if (!modelName.IsEmpty()) {
				if (!first)
					result.Append(",");
				result << "\"" << modelName << "\"";
				first = false;
			}
			currentPos = endPos + 1;
		}
		result.Append("]");
	}

	delete request;
	delete output;

	if (first)
		return CopyToBuffer(kFallbackModels, out_buf, out_len);
	return CopyToBuffer(result.String(), out_buf, out_len);
}


extern "C" status_t
ai_plugin_set_model(ai_plugin_t handle, const char* model_name)
{
	if (handle == NULL || model_name == NULL || model_name[0] == '\0')
		return B_BAD_VALUE;
	return B_OK;
}


extern "C" const char*
get_plugin_name(void)
{
	return "Ollama";
}
