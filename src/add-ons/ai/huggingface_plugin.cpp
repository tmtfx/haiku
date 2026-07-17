#include "../../../../headers/os/ai/AIPlugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>
#include <String.h>
#include <OS.h>

using namespace BPrivate::Network;

class FileWriter : public BUrlProtocolListener, public BDataIO {
public:
	FileWriter(FILE* f)
		: fFile(f)
	{
	}
	virtual ~FileWriter() {
		if (fFile) fclose(fFile);
	}

	ssize_t Write(const void* buffer, size_t size) override {
		if (!fFile) return -1;
		size_t w = fwrite(buffer, 1, size, fFile);
		fflush(fFile);
		return (ssize_t)w;
	}

	void RequestCompleted(BUrlRequest* /*caller*/, bool /*success*/) override {
		if (!fFile) return;
		fprintf(fFile, "\n<<STREAM_END>>\n"); fflush(fFile); fclose(fFile); fFile = NULL;
	}

private:
	FILE* fFile;
};

struct HFHandle : public AIPluginHandle {
    std::string mode; // "local" or "remote"
    std::string model;
    std::string model_dir;
    std::string notify_path; // for async streaming

    HFHandle() : AIPluginHandle(), mode("remote") {}
};

extern "C" ai_plugin_t ai_plugin_init(void)
{
    HFHandle* h = new (std::nothrow) HFHandle();
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    HFHandle* h = (HFHandle*)handle;
    delete h;
}

extern "C" status_t ai_plugin_list_models(const BMessage* settingsMsg, char* out_buf, size_t out_len)
{
    if (!out_buf) return B_BAD_VALUE;
    const char* modelDir = ".";
    if (settingsMsg) {
        settingsMsg->FindString("model_dir", &modelDir);
    }
    std::vector<std::string> models;
    DIR* d = opendir(modelDir);
    if (!d) {
        snprintf(out_buf, out_len, "[]");
        return B_OK;
    }
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        models.push_back(e->d_name);
    }
    closedir(d);

    std::string out = "[";
    for (size_t i = 0; i < models.size(); ++i) {
        if (i) out += ",";
        out += "\"" + models[i] + "\"";
    }
    out += "]";
    strncpy(out_buf, out.c_str(), out_len);
    out_buf[out_len - 1] = '\0';
    return B_OK;
}

extern "C" status_t ai_plugin_set_model(ai_plugin_t handle, const char* model_name)
{
    if (!handle || !model_name) return B_BAD_VALUE;
    HFHandle* h = (HFHandle*)handle;
    h->model = model_name;
    return B_OK;
}

extern "C" uint32 ai_plugin_get_capabilities(void)
{
    return AI_CAP_STREAMING;
}

extern "C" status_t ai_plugin_generate_text_sync(ai_plugin_t handle,
                                              const char* prompt,
                                              char* response_buf,
                                              size_t response_len,
                                              BMessage* contextMsg)
{
    if (!handle || !prompt || !response_buf || !contextMsg) return B_BAD_VALUE;
    HFHandle* h = (HFHandle*)handle;
    
    const char* apiKey = nullptr;
    const char* modelName = nullptr;
    const char* baseUrl = nullptr;
    contextMsg->FindString("api_key", &apiKey);
    contextMsg->FindString("model_name", &modelName);
    contextMsg->FindString("base_url", &baseUrl);

    if (!baseUrl || baseUrl[0] == '\0') {
        baseUrl = h->base_url;
    }
    if (!baseUrl || baseUrl[0] == '\0') {
        baseUrl = "https://api-inference.huggingface.co";
    }

    if (h->mode == "local") {
        snprintf(response_buf, response_len, "{\"error\":\"local inference not implemented\"}");
        return B_ERROR;
    }

    std::string url = baseUrl;
    std::string model = modelName ? modelName : (h->model.empty() ? "gpt2" : h->model);
    
    if (url.back() != '/') url += "/";
    url += "models/" + model;

    std::string payload = std::string("{\"inputs\":\"") + prompt + "\"}";

    BMallocIO* out = new BMallocIO();
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(BUrl(url.c_str(), true), out, NULL, NULL);
    if (!req) { delete out; snprintf(response_buf, response_len, "{\"error\":\"request failed\"}"); return B_ERROR; }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BMallocIO* in = new BMallocIO();
        in->WriteExactly(payload.c_str(), payload.size());
        http->AdoptInputData(in, payload.size());
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        if (apiKey && apiKey[0] != '\0') {
            BString authHeader;
            authHeader.SetToFormat("Bearer %s", apiKey);
            headers.AddHeader("Authorization", authHeader.String());
        }
        http->SetHeaders(headers);
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }

    const void* buf = out->Buffer();
    size_t len = out->BufferLength();
    if (rc != B_OK || !buf || len == 0) {
        snprintf(response_buf, response_len, "{\"error\":\"request failed\"}");
        delete req; delete out; return B_ERROR;
    }
    size_t copy_len = len < response_len-1 ? len : response_len-1;
    memcpy(response_buf, buf, copy_len);
    response_buf[copy_len] = '\0';

    delete req;
    delete out;
    return B_OK;
}

extern "C" status_t ai_plugin_generate_text_async(ai_plugin_t handle,
                                               const char* prompt,
                                               BMessage* contextMsg)
{
    if (!handle || !prompt || !contextMsg) return B_BAD_VALUE;
    HFHandle* h = (HFHandle*)handle;
    if (h->mode != "remote") return B_ERROR;

    const char* apiKey = nullptr;
    const char* modelName = nullptr;
    const char* baseUrl = nullptr;
    const char* notifyPath = nullptr;
    contextMsg->FindString("api_key", &apiKey);
    contextMsg->FindString("model_name", &modelName);
    contextMsg->FindString("base_url", &baseUrl);
    contextMsg->FindString("notify_path", &notifyPath);

    if (!notifyPath || notifyPath[0] == '\0') return B_BAD_VALUE;

    if (!baseUrl || baseUrl[0] == '\0') {
        baseUrl = h->base_url;
    }
    if (!baseUrl || baseUrl[0] == '\0') {
        baseUrl = "https://api-inference.huggingface.co";
    }

    std::string url = baseUrl;
    std::string model = modelName ? modelName : (h->model.empty() ? "gpt2" : h->model);
    if (url.back() != '/') url += "/";
    url += "models/" + model;

    std::string payload = std::string("{\"inputs\":\"") + prompt + "\"}";

    FILE* f = fopen(notifyPath, "w+");
    if (!f) return B_ERROR;

    FileWriter* listener = new FileWriter(f);
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(BUrl(url.c_str(), true), listener, listener, NULL);
    if (!req) { delete listener; return B_ERROR; }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BMallocIO* in = new BMallocIO();
        in->WriteExactly(payload.c_str(), payload.size());
        http->AdoptInputData(in, payload.size());
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        if (apiKey && apiKey[0] != '\0') {
            BString authHeader;
            authHeader.SetToFormat("Bearer %s", apiKey);
            headers.AddHeader("Authorization", authHeader.String());
        }
        http->SetHeaders(headers);
    }

    thread_id t = req->Run();
    if (t < 0) { req->Stop(); delete req; delete listener; return B_ERROR; }

    return B_OK;
}

extern "C" const char* get_plugin_name() {
    return "HuggingFace";
}
