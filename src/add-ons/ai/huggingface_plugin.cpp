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
#include <UrlSynchronousRequest.h>
#include <UrlProtocolListener.h>
#include <DataIO.h>
#include <HttpHeaders.h>
#include <HttpRequest.h>

using namespace BPrivate::Network;

// Simple writer that forwards incoming data to FILE*
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

struct HFHandle {
    std::string mode; // "local" or "remote"
    std::string api_key;
    std::string base_url;
    std::string model;
    std::string model_dir;
    std::string notify_path; // for async streaming
};

static const char* _get_json_str(const char* json, const char* key)
{
    if (!json || !key) return nullptr;
    const char* p = strstr(json, key);
    if (!p) return nullptr;
    p = strchr(p, ':');
    if (!p) return nullptr;
    p++;
    while (*p == ' ' || *p == '"' || *p == '\'') p++;
    static thread_local char buf[1024];
    int i = 0;
    while (*p && *p != '"' && *p != '\\' && *p != ',' && i + 1 < (int)sizeof(buf)) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return buf;
}

extern "C" ai_plugin_t ai_plugin_init(const char* config_json)
{
    HFHandle* h = new HFHandle();
    h->mode = "remote";
    if (config_json) {
        const char* v = _get_json_str(config_json, "mode");
        if (v && v[0]) h->mode = v;
        v = _get_json_str(config_json, "api_key");
        if (v) h->api_key = v;
        v = _get_json_str(config_json, "base_url");
        if (v) h->base_url = v;
        v = _get_json_str(config_json, "model_dir");
        if (v) h->model_dir = v;
        v = _get_json_str(config_json, "model");
        if (v) h->model = v;
        v = _get_json_str(config_json, "notify_path");
        if (v) h->notify_path = v;
    }
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle)
{
    if (!handle) return;
    delete (HFHandle*)handle;
}

extern "C" int ai_plugin_list_models(ai_plugin_t handle, char* out_buf, size_t out_len)
{
    if (!handle || !out_buf) return -1;
    HFHandle* h = (HFHandle*)handle;
    std::vector<std::string> models;
    if (h->mode == "local") {
        std::string dir = h->model_dir.size() ? h->model_dir : ".";
        DIR* d = opendir(dir.c_str());
        if (!d) {
            snprintf(out_buf, out_len, "[]");
            return 0;
        }
        struct dirent* ent;
        while ((ent = readdir(d))) {
            if (ent->d_name[0] == '.') continue;
            std::string p = dir + "/" + ent->d_name;
            struct stat st;
            if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                models.push_back(ent->d_name);
            }
        }
        closedir(d);
    } else {
        // Remote listing not implemented in this skeleton
    }
    // Build JSON array
    std::string out = "[";
    for (size_t i=0;i<models.size();++i) {
        if (i) out += ",";
        out += "\"" + models[i] + "\"";
    }
    out += "]";
    strncpy(out_buf, out.c_str(), out_len);
    out_buf[out_len-1] = '\0';
    return 0;
}

extern "C" int ai_plugin_set_model(ai_plugin_t handle, const char* model_name)
{
    if (!handle || !model_name) return -1;
    HFHandle* h = (HFHandle*)handle;
    h->model = model_name;
    return 0;
}

extern "C" int ai_plugin_update_config(ai_plugin_t handle, const char* config_json)
{
    if (!handle || !config_json) return -1;
    HFHandle* h = (HFHandle*)handle;
    const char* v = _get_json_str(config_json, "mode");
    if (v && v[0]) h->mode = v;
    v = _get_json_str(config_json, "api_key"); if (v) h->api_key = v;
    v = _get_json_str(config_json, "base_url"); if (v) h->base_url = v;
    v = _get_json_str(config_json, "model_dir"); if (v) h->model_dir = v;
    v = _get_json_str(config_json, "notify_path"); if (v) h->notify_path = v;
    return 0;
}

extern "C" const char* ai_plugin_metadata()
{
    return "{\"provider\":\"HuggingFace\",\"auth\":\"api_key\",\"capabilities\": [\"list\",\"generate_sync\",\"generate_async\"] }";
}


extern "C" int ai_plugin_generate_text_sync(ai_plugin_t handle,
                                              const char* prompt,
                                              char* response_buf,
                                              size_t response_len)
{
    if (!handle || !prompt || !response_buf) return -1;
    HFHandle* h = (HFHandle*)handle;
    if (h->mode == "local") {
        // Local inference not implemented in this skeleton
        snprintf(response_buf, response_len, "{\"error\":\"local inference not implemented\"}");
        return -1;
    }
    if (h->mode == "remote") {
        if (!h->base_url.size()) { snprintf(response_buf, response_len, "{\"error\":\"no base_url\"}"); return -1; }

        std::string url = h->base_url;
        if (h->model.size()) url += "/models/" + h->model + "/generate";
        std::string payload = std::string("{\"prompt\":\"") + prompt + "\"}";

        BMallocIO* out = new BMallocIO();
        BUrlRequest* req = BUrlProtocolRoster::MakeRequest(BUrl(url), out, NULL, NULL);
        if (!req) { delete out; snprintf(response_buf, response_len, "{\"error\":\"request failed\"}"); return -1; }

        BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
        if (http) {
            http->SetMethod(B_HTTP_POST);
            BMallocIO* in = new BMallocIO();
            in->WriteExactly(payload.c_str(), payload.size());
            http->AdoptInputData(in, payload.size());
            BHttpHeaders headers;
            headers.AddHeader("Content-Type", "application/json");
            if (h->api_key.size()) headers.AddHeader("Authorization", std::string("Bearer ") + h->api_key);
            http->SetHeaders(headers);
        }

        BUrlSynchronousRequest sync(*req);
        status_t rc = sync.Perform();

        const void* buf = out->Buffer();
        size_t len = out->BufferLength();
        if (!buf || len == 0) {
            snprintf(response_buf, response_len, "{\"error\":\"empty response\"}");
            delete req; delete out; return -1;
        }
        size_t copy_len = len < response_len-1 ? len : response_len-1;
        memcpy(response_buf, buf, copy_len);
        response_buf[copy_len] = '\0';

        delete req;
        delete out;
        return rc == B_OK ? 0 : -1;
    }

    snprintf(response_buf, response_len, "{\"error\":\"unsupported mode\"}");
    return -1;
}

extern "C" int ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt)
{
    if (!handle || !prompt) return -1;
    HFHandle* h = (HFHandle*)handle;
    if (h->mode != "remote") return -1;
    if (!h->base_url.size() || !h->notify_path.size()) return -1;

    std::string url = h->base_url;
    if (h->model.size()) url += "/models/" + h->model + "/generate";
    std::string payload = std::string("{\"prompt\":\"") + prompt + "\"}";

    FILE* f = fopen(h->notify_path.c_str(), "w+");
    if (!f) return -1;

    FileWriter* listener = new FileWriter(f);
    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(BUrl(url), listener, listener, NULL);
    if (!req) { delete listener; return -1; }

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BMallocIO* in = new BMallocIO();
        in->WriteExactly(payload.c_str(), payload.size());
        http->AdoptInputData(in, payload.size());
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        if (h->api_key.size()) headers.AddHeader("Authorization", std::string("Bearer ") + h->api_key);
        http->SetHeaders(headers);
    }

    thread_id t = req->Run();
    if (t < 0) { req->Stop(); delete req; delete listener; return -1; }

    return 0;
}
extern "C" const char* get_plugin_name() {
    return "Huggingface";
}
