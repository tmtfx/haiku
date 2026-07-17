// anthropic_plugin.cpp
// Plugin Anthropic Messages API per Haiku AI.

#include <os/ai/AIPlugin.h>
#include <os/ai/AINetworkPlugin.h>
#include <os/ai/AICommands.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
//#include <UrlProtocolListener.h>
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
#define DEFAULT_ANTHROPIC_MODEL "claude-3-5-sonnet-20241022"

/* usiamo la generica Handle in AIPlugin.h
struct AnthropicHandle {
	char* base_url;
};
*/

/*
struct AnthropicAsyncArgs {
	char* api_key;
	char* model;
	char* notify_path;
	char* base_url;
	BMessage* context_copy;
};
*/

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

/*
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
*/

static BString EscapeStringForJson(const char* input) {
    if (!input) return "";
    BString escaped;
    const char* p = input;
    while (*p) {
        switch (*p) {
            case '\\': escaped << "\\\\"; break;
            case '"':  escaped << "\\\""; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:   escaped << *p; break;
        }
        p++;
    }
    return escaped;
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

/*
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
*/

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


static void SerializeBMessageToJson(const BMessage* msg, BString& outJson) {
    outJson << "{";
    bool first = true;
    
    char* field_name = nullptr;
    uint32 field_code = 0;
    int32 field_count = 0;
    
    for (int which = 0; msg->GetInfo(B_ANY_TYPE, which, 
            (char**)&field_name, &field_code, &field_count) == B_OK; which++) {
        
        if (!first) outJson << ",";
        first = false;
        
        outJson << "\"" << field_name << "\":";
        
        if (field_code == B_MESSAGE_TYPE) {
            BMessage subMsg;
            if (msg->FindMessage(field_name, &subMsg) == B_OK) {
                BString subJson;
                SerializeBMessageToJson(&subMsg, subJson);
                outJson << subJson;
            } else {
                outJson << "{}";
            }
        } else if (field_code == B_STRING_TYPE || field_code == B_CHAR_TYPE) {
            const char* strVal = msg->FindString(field_name);
            if (strVal) {
                BString escaped(strVal);
                escaped.ReplaceAll("\\", "\\\\");
                escaped.ReplaceAll("\"", "\\\"");
                escaped.ReplaceAll("\n", "\\n");
                escaped.ReplaceAll("\r", "\\r");
                escaped.ReplaceAll("\t", "\\t");
                outJson << "\"" << escaped << "\"";
            } else {
                outJson << "\"\"";
            }
        } else if (field_code == B_BOOL_TYPE) {
            bool boolVal = false;
            msg->FindBool(field_name, &boolVal);
            outJson << (boolVal ? "true" : "false");
        } else {
            int32 intVal = 0;
            msg->FindInt32(field_name, &intVal);
            outJson << intVal;
        }
    }
    
    outJson << "}";
}


static void ConvertBMessageToAnthropicToolsJson(const BMessage* toolsMsg, BString& outJson)
{
    outJson = "[";

    BMessage tool;
    int32 i = 0;
    bool first = true;

    while (toolsMsg->FindMessage("tool", i, &tool) == B_OK) {
        if (!first) outJson << ",";
        first = false;

        const char* name = tool.FindString("name");
        const char* desc = tool.FindString("description");

        BString escapedDesc(desc ? desc : "");
        escapedDesc.ReplaceAll("\\", "\\\\");
        escapedDesc.ReplaceAll("\"", "\\\"");
        escapedDesc.ReplaceAll("\n", "\\n");
        escapedDesc.ReplaceAll("\r", "\\r");
        escapedDesc.ReplaceAll("\t", "\\t");

        outJson << "{";
        outJson << "\"name\":\"" << name << "\",";
        outJson << "\"description\":\"" << escapedDesc << "\"";

        BMessage params;
        tool.FindMessage("parameters", &params);
        if (!params.IsEmpty()) {
            BMessage requiredMsg;
            BString reqFieldStr = "";
            const char* fStr = nullptr;
            if (params.FindString("required", &fStr) == B_OK && fStr != nullptr) {
                reqFieldStr = fStr;
            } else if (params.FindMessage("required", &requiredMsg) == B_OK) {
                const char* f = requiredMsg.FindString("0");
                if (f) reqFieldStr = f;
            }
            params.RemoveName("required");

            BString jsonParams;
            SerializeBMessageToJson(&params, jsonParams);
            
            if (!reqFieldStr.IsEmpty()) {
                int32 lastBrace = jsonParams.FindLast("}");
                if (lastBrace != B_ERROR) {
                    jsonParams.Truncate(lastBrace);
                    jsonParams << ",\"required\":[\"" << reqFieldStr << "\"]}";
                }
            }

            fprintf(stderr, "[DEBUG ANTHROPIC TOOLS] Tool '%s' - jsonParams finale generato:\n%s\n", 
                    name, jsonParams.String());

            outJson << ",\"input_schema\":" << jsonParams;
        } else {
            outJson << ",\"input_schema\":{\"type\":\"object\",\"properties\":{}}";
        }

        outJson << "}"; // fine tool
        i++;
    }

    outJson << "]";

    fprintf(stderr, "[DEBUG ANTHROPIC TOOLS] PAYLOAD STRUMENTI COMPLETO INVIATO ALL'API:\n%s\n", outJson.String());
}


static void AppendToolCallToContext(BMessage* context, const char* name, const BMessage* argsMsg, const char* toolCallId) {
    BMessage messagesMsg;
    if (context->FindMessage("messages", &messagesMsg) != B_OK) {
        context->AddMessage("messages", &messagesMsg);
    }
    int32 i = 0; BMessage dummy;
    while (messagesMsg.FindMessage("msg", i, &dummy) == B_OK) i++;
    
    BMessage toolCallMsg;
    toolCallMsg.AddString("type", "functionCall");
    toolCallMsg.AddString("name", name);
    if (argsMsg) {
        toolCallMsg.AddMessage("args", argsMsg);
    }
    if (toolCallId) {
        toolCallMsg.AddString("tool_call_id", toolCallId);
    }
    
    messagesMsg.AddMessage("msg", &toolCallMsg);
    context->RemoveName("messages");
    context->AddMessage("messages", &messagesMsg);
}


static void AppendToolResponseToContext(BMessage* context, const char* name, const char* responseJson, const char* toolCallId) {
    BMessage messagesMsg;
    if (context->FindMessage("messages", &messagesMsg) != B_OK) {
        context->AddMessage("messages", &messagesMsg);
    }
    int32 i = 0; BMessage dummy;
    while (messagesMsg.FindMessage("msg", i, &dummy) == B_OK) i++;
    
    BMessage toolRespMsg;
    toolRespMsg.AddString("type", "functionResponse");
    toolRespMsg.AddString("name", name);
    toolRespMsg.AddString("response", responseJson);
    if (toolCallId) {
        toolRespMsg.AddString("tool_call_id", toolCallId);
    }
    
    messagesMsg.AddMessage("msg", &toolRespMsg);
    context->RemoveName("messages");
    context->AddMessage("messages", &messagesMsg);
}

/*
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
*/

static void BuildAnthropicPayload(const BMessage* chatContext, const char* explicitPrompt, BString& outPayload, bool stream = false)
{
    const char* model = nullptr;
    if (chatContext) chatContext->FindString("model_name", &model);
    if (!model || model[0] == '\0') model = DEFAULT_ANTHROPIC_MODEL;

    outPayload.SetTo("{\n");
    BString modelLine;
    modelLine.SetToFormat("  \"model\": \"%s\",\n", model);
    outPayload << modelLine;
    outPayload.Append("  \"max_tokens\": 4096,\n");
    
    if (stream) {
        outPayload.Append("  \"stream\": true,\n");
    }
    outPayload.Append("  \"messages\": [\n");

    bool first = true;
    BMessage historyMsg;
    
    // 1. Inietta lo storico se presente nel contesto nativo
    if (chatContext && chatContext->FindMessage("messages", &historyMsg) == B_OK) {
        int32 i = 0;
        BMessage msgTurn;
        while (historyMsg.FindMessage("msg", i++, &msgTurn) == B_OK
            || historyMsg.FindMessage(BString().SetToFormat("%ld", (long)(i-1)).String(), &msgTurn) == B_OK) {
            
            const char* type = nullptr;
            msgTurn.FindString("type", &type);

            if (type && strcmp(type, "functionCall") == 0) {
                const char* name = msgTurn.FindString("name");
                const char* toolCallId = msgTurn.FindString("tool_call_id");
                if (!toolCallId || toolCallId[0] == '\0') toolCallId = "call_dummy";

                BString cleanArgs;
                BMessage argsMsg;
                if (msgTurn.FindMessage("args", &argsMsg) == B_OK) {
                    SerializeBMessageToJson(&argsMsg, cleanArgs);
                } else {
                    const char* argsStr = msgTurn.FindString("args");
                    cleanArgs = (argsStr && argsStr[0] != '\0' ? argsStr : "{}");
                }

                if (!first) outPayload.Append(",\n");
                outPayload << "    {\n"
                           << "      \"role\": \"assistant\",\n"
                           << "      \"content\": [\n"
                           << "        {\n"
                           << "          \"type\": \"tool_use\",\n"
                           << "          \"id\": \"" << toolCallId << "\",\n"
                           << "          \"name\": \"" << name << "\",\n"
                           << "          \"input\": " << cleanArgs << "\n"
                           << "        }\n"
                           << "      ]\n"
                           << "    }";
                first = false;
            }
            else if (type && strcmp(type, "functionResponse") == 0) {
                const char* name = msgTurn.FindString("name");
                const char* response = msgTurn.FindString("response");
                const char* toolCallId = msgTurn.FindString("tool_call_id");
                if (!toolCallId || toolCallId[0] == '\0') toolCallId = "call_dummy";

                BString formattedResponse;
                BString respStr(response ? response : "");
                respStr.Trim();
                
                BString escapedResp = EscapeStringForJson(respStr.String());

                if (!first) outPayload.Append(",\n");
                outPayload << "    {\n"
                           << "      \"role\": \"user\",\n"
                           << "      \"content\": [\n"
                           << "        {\n"
                           << "          \"type\": \"tool_result\",\n"
                           << "          \"tool_use_id\": \"" << toolCallId << "\",\n"
                           << "          \"content\": \"" << escapedResp << "\"\n"
                           << "        }\n"
                           << "      ]\n"
                           << "    }";
                first = false;
            }
            else {
                // Messaggio standard di testo
                const char* role = nullptr;
                const char* content = nullptr;
                msgTurn.FindString("role", &role);
                if (msgTurn.FindString("content", &content) != B_OK) {
                    msgTurn.FindString("text", &content);
                }

                if (role && content) {
                    BString roleStr(role);
                    if (roleStr == "model" || roleStr == "system") {
                        roleStr = "assistant"; 
                    }
                    
                    if (roleStr == "user" || roleStr == "assistant") {
                        if (!first) outPayload.Append(",\n");
                        
                        BString escapedContent = EscapeStringForJson(content);
                        
                        BString modelLine;
                        modelLine.SetToFormat("    {\"role\": \"%s\", \"content\": \"%s\"}", roleStr.String(), escapedContent.String());
                        outPayload << modelLine;
                        first = false;
                    }
                }
            }
        }
    }

    // 2. Se viene passato un prompt esplicito (non ancora salvato nello storico), lo appende alla fine
    if (explicitPrompt && explicitPrompt[0] != '\0') {
        if (!first) outPayload.Append(",\n");
        BString escapedPrompt = EscapeStringForJson(explicitPrompt);
        
        BString modelLine;
        modelLine.SetToFormat("    {\"role\": \"user\", \"content\": \"%s\"}", escapedPrompt.String());
        outPayload << modelLine;
    }

    outPayload.Append("\n  ]");
    
    BString systemPrompt;
    if (chatContext) {
        const char* sys = nullptr;
        if (chatContext->FindString("system_prompt", &sys) == B_OK && sys != nullptr) {
            systemPrompt = sys;
        } else if (chatContext->FindString("instructions", &sys) == B_OK && sys != nullptr) {
            systemPrompt = sys;
        }
    }
    
    if (systemPrompt.Length() > 0) {
        outPayload << ",\n  \"system\": \"" << EscapeStringForJson(systemPrompt.String()) << "\"";
    }

    outPayload.Append("\n}");
}

/*
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
*/


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

/*
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
*/

static status_t
anthropic_stream_thread_func(void* data)
{
	fprintf(stderr, "[ANTHROPIC STREAM WORKER] Thread avviato.\n");
	AsyncArgs* args = (AsyncArgs*)data;
	if (args == NULL)
		return B_BAD_VALUE;

	BMessage replyTools;
	bool mcpActive = false;
	bool executionLoop = true;

	// Controllo di sicurezza sulla chiave API
	if (args->api_key == NULL || args->api_key[0] == '\0') {
		fprintf(stderr, "[ANTHROPIC STREAM WORKER] ERRORE CRITICO: API key assente!\n");
		if (args->notify_path != NULL) {
			BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE);
			BString endMarker = "<<STREAM_END>>";
			streamFile.Write(endMarker.String(), endMarker.Length());
		}
		goto thread_cleanup;
	}

	// === CONTROLLO CAPABILITY VIA MESSENGER ===
	if (args->server_messenger.IsValid()) {
		BMessage reqTools(MSG_MCP_GET_TOOLS); 
		const char* ctxId = NULL;
		if (args->context_copy != NULL) {
			args->context_copy->FindString("context_id", &ctxId);
		}
		if (ctxId != NULL) {
			reqTools.AddString("context_id", ctxId);
		}
		
		if (args->server_messenger.SendMessage(&reqTools, &replyTools) == B_OK) {
			if (replyTools.HasMessage("tool", 0)) {
				mcpActive = true;
			}
		}
	}

	// === CASO 1: MODALITÀ STANDARD (STREAMING NATIVO RETROCOMPATIBILE) ===
	if (!mcpActive) {
		goto fallback_to_standard;
	}

	// === CASO 2: MODALITÀ MCP ATTIVA (LOOP STRUMENTI VIA IPC MESSENGER) ===
	fprintf(stderr, "[ANTHROPIC STREAM WORKER] Modalità MCP Attiva: Avvio loop di interazione strumenti.\n");
	
	executionLoop = true;
	while (executionLoop) {
		// Controllo interruzione preventivo via IPC
		if (args->server_messenger.IsValid()) {
			BMessage checkAbortMsg('CHAB'); // MSG_CHECK_ABORT
			const char* ctxId = NULL;
			if (args->context_copy != NULL) {
				args->context_copy->FindString("context_id", &ctxId);
			}
			if (ctxId != NULL) {
				checkAbortMsg.AddString("context_id", ctxId);
			}
			
			BMessage abortReply;
			if (args->server_messenger.SendMessage(&checkAbortMsg, &abortReply) == B_OK) {
				int32 status = B_OK;
				if (abortReply.FindInt32("status", &status) == B_OK && status == B_CANCELED) {
					fprintf(stderr, "[ANTHROPIC STREAM WORKER] Rilevata interruzione asincrona dall'utente. Esco.\n");
					executionLoop = false;
					break;
				}
			}
		}

		BString url = BuildMessagesUrl(args->base_url);

		BString payload;
		// In modalità MCP non usiamo lo streaming (stream = false)
		BuildAnthropicPayload(args->context_copy, NULL, payload, false);

		if (mcpActive) {
			BString anthropicToolsJson;
			ConvertBMessageToAnthropicToolsJson(&replyTools, anthropicToolsJson);

			if (anthropicToolsJson.Length() > 0) {
				int32 lastCloseBrace = payload.FindLast("}");
				if (lastCloseBrace != B_ERROR) {
					payload.Truncate(lastCloseBrace);
					payload << ",\"tools\":" << anthropicToolsJson << "}";
				}
			}
		}

		BMallocIO outNetworkData;
		SyncListener syncListener;
		BUrl bUrl(url.String(), true);
		BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &outNetworkData, &syncListener, NULL);
		if (!req) { 
			executionLoop = false; 
			break; 
		}

		BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
		if (http != NULL) {
			http->SetMethod(B_HTTP_POST);
			BHttpHeaders headers;
			headers.AddHeader("Content-Type", "application/json");
			headers.AddHeader("anthropic-version", "2023-06-01");
			headers.AddHeader("x-api-key", args->api_key);
			http->SetHeaders(headers);
			
			BMallocIO* input = new BMallocIO();
			input->WriteExactly(payload.String(), payload.Length());
			http->AdoptInputData(input, payload.Length());
		}

		thread_id netThread = req->Run();
		if (netThread >= 0) { 
			status_t rc; 
			wait_for_thread(netThread, &rc); 
		}
		delete req;

		BString rawResponse((const char*)outNetworkData.Buffer(), outNetworkData.BufferLength());
		BMessage parsedJson;
		
		if (BJson::Parse(rawResponse.String(), parsedJson) != B_OK) {
			fprintf(stderr, "[ANTHROPIC STREAM WORKER] Fallito il parsing JSON della risposta di rete.\n");
			executionLoop = false;
			break;
		}

		BMessage errorObj;
		if (parsedJson.FindMessage("error", &errorObj) == B_OK) {
			const char* errMsg = NULL;
			errorObj.FindString("message", &errMsg);
			if (errMsg != NULL) {
				fprintf(stderr, "[ANTHROPIC WORKER] Dettaglio errore API Anthropic: %s\n", errMsg);
				
				// Se contiene indicazioni di problemi coi tool
				if (BString(errMsg).FindFirst("tool") != B_ERROR) {
					fprintf(stderr, "[ANTHROPIC WORKER] Rifiuto MCP. Fallback alla Modalità Standard.\n");
					mcpActive = false;
					executionLoop = false;
					goto fallback_to_standard;
				}
			}
		}

		BMessage contentMsg;
		bool foundTool = false;

		if (parsedJson.FindMessage("content", &contentMsg) == B_OK) {
			int32 blockIndex = 0;
			BMessage block;

			while (contentMsg.FindMessage(BString().SetToFormat("%ld", (long)blockIndex).String(), &block) == B_OK
				|| contentMsg.FindMessage("msg", blockIndex, &block) == B_OK) {
				
				const char* typeStr = NULL;
				block.FindString("type", &typeStr);
				if (typeStr != NULL && strcmp(typeStr, "tool_use") == 0) {
					const char* toolName = NULL;
					const char* toolCallId = NULL;
					block.FindString("name", &toolName);
					block.FindString("id", &toolCallId);
					
					BMessage inputMsg;
					BString argsJson;
					if (block.FindMessage("input", &inputMsg) == B_OK) {
						SerializeBMessageToJson(&inputMsg, argsJson);
					} else {
						argsJson = "{}";
					}

					fprintf(stderr, "[ANTHROPIC MCP] Claude richiede lo strumento: %s con argomenti: %s\n", toolName, argsJson.String());

					// Invochiamo lo strumento mandando un messaggio sincrono all'ai_server
					BMessage reqExec(MSG_EXECUTE_TOOL);
					reqExec.AddString("name", toolName);
					reqExec.AddMessage("arguments", &inputMsg);
					
					const char* ctxId = NULL;
					if (args->context_copy != NULL) {
						args->context_copy->FindString("context_id", &ctxId);
					}
					if (ctxId != NULL) {
						reqExec.AddString("context_id", ctxId);
					}
					
					BMessage replyExec;
					BString toolResultBuf;
					
					if (args->server_messenger.SendMessage(&reqExec, &replyExec) == B_OK) {
						const char* resStr = replyExec.FindString("result");
						if (resStr != NULL && strlen(resStr) > 0) {
							BString testStr(resStr);
							testStr.Trim();
							if (testStr.StartsWith("{") || testStr.StartsWith("[")) {
								toolResultBuf = resStr;
							} else {
								BString escapedRes = EscapeStringForJson(resStr);
								toolResultBuf.SetToFormat("{\"output\":\"%s\"}", escapedRes.String());
							}
						} else {
							toolResultBuf = "{\"error\":\"Il comando sul server ha restituito una risposta vuota.\"}";
						}
					} else {
						toolResultBuf = "{\"error\":\"Esecuzione dello strumento fallita via IPC BMessenger\"}";
					}

					// Aggiorniamo la history clonata in memoria per il prossimo turno del loop
					AppendToolCallToContext(args->context_copy, toolName, &inputMsg, toolCallId);
					AppendToolResponseToContext(args->context_copy, toolName, toolResultBuf.String(), toolCallId);
					
					foundTool = true;
					break; // Esci dal loop dei blocchi e procedi al prossimo turno della conversazione
				}
				blockIndex++;
			}

			if (!foundTool) {
				// Let's collect all text content
				BString fullText;
				blockIndex = 0;
				BMessage block;
				while (contentMsg.FindMessage(BString().SetToFormat("%ld", (long)blockIndex).String(), &block) == B_OK
					|| contentMsg.FindMessage("msg", blockIndex, &block) == B_OK) {
					
					const char* typeStr = NULL;
					block.FindString("type", &typeStr);
					if (typeStr != NULL && strcmp(typeStr, "text") == 0) {
						const char* textVal = NULL;
						if (block.FindString("text", &textVal) == B_OK && textVal != NULL) {
							fullText.Append(textVal);
						}
					}
					blockIndex++;
				}
				
				if (fullText.Length() > 0) {
					fprintf(stderr, "[ANTHROPIC MCP] Risposta testuale finale ricevuta.\n");
					BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
					if (streamFile.InitCheck() == B_OK) {
						streamFile.Write(fullText.String(), fullText.Length());
					} else {
						fprintf(stderr, "[ANTHROPIC MCP] ERRORE: Impossibile creare/aprire il file di notifica!\n");
					}
					executionLoop = false;
				} else {
					executionLoop = false;
				}
			}
		} else {
			fprintf(stderr, "[ANTHROPIC STREAM WORKER] ERRORE: Risposta di rete non valida o priva di 'content'.\n");
			fprintf(stderr, "[ANTHROPIC STREAM WORKER] Risposta grezza ricevuta:\n%s\n", rawResponse.String());
			executionLoop = false;
		}
	}
	goto thread_post_actions;


fallback_to_standard:
	{
		fprintf(stderr, "[ANTHROPIC STREAM WORKER] Modalità Standard: Avvio Streaming Diretto.\n");
		
		BString url = BuildMessagesUrl(args->base_url);
		PrepareContextForStreaming(args->context_copy, NULL, args->model);

		BString payload;
		BuildAnthropicPayload(args->context_copy, NULL, payload, true);

		AnthropicStreamTarget streamTarget(args->notify_path);
		CompletionListener listener(args->notify_path);
		BUrl bUrl(url.String(), true);
		BUrlRequest* req = BUrlProtocolRoster::MakeRequest(bUrl, &streamTarget, &listener, NULL);
		if (req != NULL) {
			BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
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
			thread_id thread = req->Run();
			if (thread >= 0) { 
				status_t rc; 
				wait_for_thread(thread, &rc); 
			}
			delete req;
		}
	}

thread_post_actions:
	if (args->context_copy != NULL && !args->context_copy->HasString("title")) {
		BMessage messagesMsg;
		const char* firstPromptText = NULL;

		if (args->context_copy->FindMessage("messages", &messagesMsg) == B_OK) {
			int32 msgCount = 0;
			BMessage msgItem;
			
			while (messagesMsg.FindMessage("msg", msgCount, &msgItem) == B_OK || 
				   messagesMsg.FindMessage(BString().SetToFormat("%d", msgCount).String(), &msgItem) == B_OK) {
				
				const char* role = msgItem.FindString("role");
				const char* content = msgItem.FindString("content");
				if (!content) msgItem.FindString("text", &content);
				
				if (role && strcmp(role, "user") == 0 && content && content[0] != '\0') {
					firstPromptText = content;
				}
				msgCount++;
			}
		}

		if (firstPromptText && firstPromptText[0] != '\0') {
			BString autoTitle(firstPromptText);
			autoTitle.Trim();
			if (autoTitle.Length() > 30) {
				autoTitle.Truncate(30);
				autoTitle << "...";
			}
			
			args->context_copy->RemoveName("title");
			args->context_copy->AddString("title", autoTitle.String());
			
			if (args->server_messenger.IsValid()) {
				BMessage titleUpdateMsg('UTIT');
				titleUpdateMsg.AddString("title", autoTitle.String());
				args->server_messenger.SendMessage(&titleUpdateMsg);
			}
			fprintf(stderr, "[ANTHROPIC ASYNC] Auto-titolo generato: '%s'\n", autoTitle.String());
		}
	}
	{
		BFile streamFile(args->notify_path, B_WRITE_ONLY | B_CREATE_FILE | B_OPEN_AT_END);
		if (streamFile.InitCheck() == B_OK) {
			BString endMarker = "<<STREAM_END>>";
			streamFile.Write(endMarker.String(), endMarker.Length());
		}
	}

thread_cleanup:
	fprintf(stderr, "[ANTHROPIC STREAM WORKER] Pulizia e chiusura thread worker.\n");
	delete args; 
	return B_OK;
}


extern "C" ai_plugin_t
ai_plugin_init(void)
{
	/*AIPluginHandle* handle = (AIPluginHandle*)malloc(sizeof(AnthropicHandle));
	if (handle == NULL)
		return NULL;

	handle->base_url = NULL;
	return (ai_plugin_t)handle;*/
	AIPluginHandle* h = new(std::nothrow) AIPluginHandle();
    return (ai_plugin_t)h;
}


extern "C" void
ai_plugin_free(ai_plugin_t handle)
{
	AIPluginHandle* typedHandle = (AIPluginHandle*)handle;
	if (typedHandle == NULL)
		return;

	if (typedHandle->base_url != NULL)
		free(typedHandle->base_url);
	free(typedHandle);
}

/*
extern "C" uint32
ai_plugin_get_capabilities(void)
{
	return AI_CAP_STREAMING;
}
*/

extern "C" uint32
ai_plugin_get_capabilities(void)
{
	return AI_CAP_STREAMING | AI_CAP_MCP;
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

	AIPluginHandle* typedHandle = (AIPluginHandle*)handle;
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

/*
extern "C" status_t
ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt,
	BMessage* contextMsg)
{
	AIPluginHandle* typedHandle = (AIPluginHandle*)handle;
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
*/

extern "C" status_t
ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt,
	BMessage* contextMsg)
{
	AIPluginHandle* typedHandle = (AIPluginHandle*)handle;
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

	AsyncArgs* args = new (std::nothrow) AsyncArgs();
	if (args == NULL)
		return B_NO_MEMORY;

	args->api_key = dupstr_or_null(apiKey);
	args->model = dupstr_or_null(
		(model != NULL && model[0] != '\0') ? model : DEFAULT_ANTHROPIC_MODEL);
	args->notify_path = dupstr_or_null(notifyPath);
	args->base_url = typedHandle != NULL ? dupstr_or_null(typedHandle->base_url) : NULL;
	args->context_copy = new BMessage(*contextMsg);
	if (args->context_copy == NULL) {
		delete args;
		return B_NO_MEMORY;
	}

	BMessenger serverMessenger;
	if (contextMsg->FindMessenger("server_messenger", &serverMessenger) == B_OK) {
		args->server_messenger = serverMessenger;
	} else {
		args->server_messenger = BMessenger();
	}

	PrepareContextForStreaming(args->context_copy, prompt, args->model);

	thread_id thread = spawn_thread(anthropic_stream_thread_func,
		"anthropic_stream_worker", B_NORMAL_PRIORITY, args);
	if (thread < B_OK) {
		delete args;
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
		= "[\"claude-3-opus-20240229\",\"claude-3-5-sonnet-20241022\",\"claude-3-5-haiku-20241022\"]";
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
