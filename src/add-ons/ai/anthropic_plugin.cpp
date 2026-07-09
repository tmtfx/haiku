// anthropic_plugin.cpp
// Plugin Anthropic Messages API per Haiku AI.

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

#define DEFAULT_ANTHROPIC_URL   "https://api.anthropic.com"
#define DEFAULT_ANTHROPIC_MODEL "claude-sonnet-4-5"

struct AnthropicHandle {
	char* base_url;
};

struct AnthropicAsyncArgs {
	char* api_key;
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
FreeAsyncArgs(AnthropicAsyncArgs* args)
{
	if (args == NULL)
		return;

	if (args->api_key != NULL)
		free(args->api_key);
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
BuildMessagesUrl(const char* baseUrl)
{
	BString url = (baseUrl != NULL && baseUrl[0] != '\0') ? baseUrl : DEFAULT_ANTHROPIC_URL;
	if (!url.EndsWith("/"))
		url.Append("/");
	url.Append("v1/messages");
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
ExtractAnthropicDeltaText(const char* line, BString& out)
{
	if (line == NULL)
		return false;

	const char* delta = strstr(line, "\"text_delta\"");
	if (delta == NULL)
		return false;

	if (ExtractJsonStringValue(delta, ",\"text\":\"", out))
		return true;
	return ExtractJsonStringValue(delta, "\"text\":\"", out);
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
BuildAnthropicPayload(const BMessage* config, const char* currentPrompt,
	BString& outPayload, bool stream)
{
	const char* model = NULL;
	if (config != NULL)
		config->FindString("model_name", &model);
	if (model == NULL || model[0] == '\0')
		model = DEFAULT_ANTHROPIC_MODEL;

	outPayload.SetTo("{");
	outPayload << "\"model\":\"";
	AppendEscapedJsonString(outPayload, model);
	outPayload << "\",\"max_tokens\":4096,";
	if (stream)
		outPayload << "\"stream\":true,";
	outPayload << "\"messages\":[";

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

			if (role != NULL && content != NULL && content[0] != '\0'
				&& (strcmp(role, "user") == 0 || strcmp(role, "assistant") == 0)) {
				if (!first)
					outPayload.Append(",");

				outPayload << "{\"role\":\"";
				AppendEscapedJsonString(outPayload, role);
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


class AnthropicStreamTarget : public BDataIO {
public:
	AnthropicStreamTarget(const char* notifyPath)
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

			if (line.IsEmpty() || line.StartsWith("event:"))
				continue;
			if (!line.StartsWith("data: "))
				continue;
			if (line == "data: [DONE]")
				continue;
			if (line.FindFirst("\"message_stop\"") != B_ERROR)
				continue;

			BString token;
			if (ExtractAnthropicDeltaText(line.String(), token) && token.Length() > 0)
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
anthropic_stream_thread_func(void* data)
{
	AnthropicAsyncArgs* args = (AnthropicAsyncArgs*)data;
	if (args == NULL)
		return B_BAD_VALUE;

	fprintf(stderr, "[ANTHROPIC PLUGIN] streaming worker avviato\n");

	if (args->notify_path == NULL || args->notify_path[0] == '\0') {
		fprintf(stderr, "[ANTHROPIC PLUGIN] notify_path mancante\n");
		FreeAsyncArgs(args);
		return B_BAD_VALUE;
	}

	if (args->api_key == NULL || args->api_key[0] == '\0') {
		fprintf(stderr, "[ANTHROPIC PLUGIN] api_key mancante\n");
		WriteStreamEnd(args->notify_path);
		FreeAsyncArgs(args);
		return B_BAD_VALUE;
	}

	BString url = BuildMessagesUrl(args->base_url);
	PrepareContextForStreaming(args->context_copy, NULL, args->model);

	BString payload;
	BuildAnthropicPayload(args->context_copy, NULL, payload, true);

	AnthropicStreamTarget streamTarget(args->notify_path);
	CompletionListener listener(args->notify_path);
	BUrlRequest* request = BUrlProtocolRoster::MakeRequest(
		BUrl(url.String(), false), &streamTarget, &listener, NULL);
	if (request == NULL) {
		fprintf(stderr, "[ANTHROPIC PLUGIN] creazione request streaming fallita\n");
		WriteStreamEnd(args->notify_path);
		FreeAsyncArgs(args);
		return B_ERROR;
	}

	BHttpRequest* http = dynamic_cast<BHttpRequest*>(request);
	if (http != NULL) {
		http->SetMethod(B_HTTP_POST);

		BHttpHeaders headers;
		headers.AddHeader("Content-Type", "application/json");
		headers.AddHeader("anthropic-version", "2023-06-01");
		headers.AddHeader("x-api-key", args->api_key);
		http->SetHeaders(headers);

		BMemoryIO* input = new BMemoryIO(payload.String(), payload.Length());
		http->AdoptInputData(input, payload.Length());
	}

	thread_id thread = request->Run();
	if (thread >= 0) {
		status_t rc = B_ERROR;
		wait_for_thread(thread, &rc);
		fprintf(stderr, "[ANTHROPIC PLUGIN] streaming terminato rc=%ld\n", (long)rc);
	} else {
		fprintf(stderr, "[ANTHROPIC PLUGIN] avvio thread streaming fallito\n");
		WriteStreamEnd(args->notify_path);
	}

	delete request;
	FreeAsyncArgs(args);
	return B_OK;
}


extern "C" ai_plugin_t
ai_plugin_init(const BMessage* settingsMsg)
{
	AnthropicHandle* handle = (AnthropicHandle*)malloc(sizeof(AnthropicHandle));
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
	AnthropicHandle* typedHandle = (AnthropicHandle*)handle;
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

	const char* apiKey = NULL;
	if (contextMsg != NULL)
		contextMsg->FindString("api_key", &apiKey);
	if (apiKey == NULL || apiKey[0] == '\0') {
		CopyToBuffer("[anthropic_plugin] error: no API key provided",
			response_buf, response_len);
		return B_BAD_VALUE;
	}

	AnthropicHandle* typedHandle = (AnthropicHandle*)handle;
	BString url = BuildMessagesUrl(typedHandle != NULL ? typedHandle->base_url : NULL);

	BString payload;
	BuildAnthropicPayload(contextMsg, prompt, payload, false);
	fprintf(stderr, "[ANTHROPIC PLUGIN] sync request verso %s\n", url.String());

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
		headers.AddHeader("anthropic-version", "2023-06-01");
		headers.AddHeader("x-api-key", apiKey);
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

	bool extracted = false;
	if (statusCode == 200 && length > 0) {
		BMessage parsedJson;
		if (BJson::Parse(rawResponse.String(), parsedJson) == B_OK) {
			BMessage contentArray;
			BMessage firstItem;
			const char* text = NULL;

			if (parsedJson.FindMessage("content", &contentArray) == B_OK
				&& contentArray.FindMessage("0", &firstItem) == B_OK
				&& firstItem.FindString("text", &text) == B_OK) {
				CopyToBuffer(text, response_buf, response_len);
				extracted = true;
			}
		}
	}

	if (!extracted) {
		if (length > 0)
			CopyToBuffer(rawResponse.String(), response_buf, response_len);
		else
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
	AnthropicHandle* typedHandle = (AnthropicHandle*)handle;
	if (contextMsg == NULL)
		return B_BAD_VALUE;

	const char* apiKey = NULL;
	const char* model = NULL;
	const char* notifyPath = NULL;
	contextMsg->FindString("api_key", &apiKey);
	contextMsg->FindString("model_name", &model);
	contextMsg->FindString("notify_path", &notifyPath);

	if (apiKey == NULL || apiKey[0] == '\0' || notifyPath == NULL
		|| notifyPath[0] == '\0') {
		return B_BAD_VALUE;
	}

	AnthropicAsyncArgs* args = (AnthropicAsyncArgs*)malloc(sizeof(AnthropicAsyncArgs));
	if (args == NULL)
		return B_NO_MEMORY;

	args->api_key = dupstr_or_null(apiKey);
	args->model = dupstr_or_null(
		(model != NULL && model[0] != '\0') ? model : DEFAULT_ANTHROPIC_MODEL);
	args->notify_path = dupstr_or_null(notifyPath);
	args->base_url = typedHandle != NULL ? dupstr_or_null(typedHandle->base_url) : NULL;
	args->context_copy = new BMessage(*contextMsg);
	if (args->context_copy == NULL) {
		FreeAsyncArgs(args);
		return B_NO_MEMORY;
	}

	PrepareContextForStreaming(args->context_copy, prompt, args->model);

	thread_id thread = spawn_thread(anthropic_stream_thread_func,
		"anthropic_stream_worker", B_NORMAL_PRIORITY, args);
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
	(void)settingsMsg;
	static const char* kModels
		= "[\"claude-opus-4-5\",\"claude-sonnet-4-5\",\"claude-haiku-4-5\"]";
	return CopyToBuffer(kModels, out_buf, out_len);
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
	return "Claude (Anthropic)";
}
