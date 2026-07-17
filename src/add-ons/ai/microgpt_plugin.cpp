// microgpt_plugin.cpp
// Wrapper plugin for microgpt-c that discovers checkpoints and can load them.

#include "../../../../headers/os/ai/AIPlugin.h"
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

// Include microgpt public header (build system adds sources so header is available)
#include "../../../../third_party/microgpt-c/src/microgpt.h"
#include "../../../../third_party/microgpt-c/src/microgpt_organelle.h"

struct MicrogptHandle {
    std::string model_dir;
    std::string active_model;
    Model* loaded_model;
    Organelle* organelle;
    std::string notify_path; // path to append streaming output when asked
};

static std::vector<std::string> scan_models(const char* dir) {
    std::vector<std::string> out;
    if (!dir) return out;
    DIR* d = opendir(dir);
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        // Only include regular files (likely checkpoints) and directories
        std::string p = std::string(dir) + "/" + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) == 0) {
            if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) {
                out.emplace_back(e->d_name);
            }
        }
    }
    closedir(d);
    return out;
}

static bool file_exists(const std::string& path) {
    struct stat st;
    return (stat(path.c_str(), &st) == 0) && S_ISREG(st.st_mode);
}

extern "C" ai_plugin_t ai_plugin_init(void) {
    MicrogptHandle* h = new MicrogptHandle();
    h->loaded_model = NULL;
    h->organelle = NULL;
    return (ai_plugin_t)h;
}

extern "C" void ai_plugin_free(ai_plugin_t handle) {
    MicrogptHandle* h = (MicrogptHandle*)handle;
    if (!h) return;
    if (h->organelle) {
        if (h->organelle->vocab.chars) free(h->organelle->vocab.chars);
        free(h->organelle);
        h->organelle = NULL;
    }
    if (h->loaded_model) {
        model_free(h->loaded_model);
        h->loaded_model = NULL;
    }
    delete h;
}

extern "C" int ai_plugin_generate_text_sync(ai_plugin_t handle,
                                              const char* prompt,
                                              char* response_buf,
                                              size_t response_len) {
    if (!handle || !response_buf) return -1;
    MicrogptHandle* h = (MicrogptHandle*)handle;

    if (h->organelle) {
        // Use organelle_generate for inference. Build a config from defaults.
        MicrogptConfig cfg = microgpt_default_config();
        cfg.n_embd = N_EMBD;
        cfg.n_head = N_HEAD;
        cfg.mlp_dim = MLP_DIM;
        cfg.n_layer = N_LAYER;
        cfg.block_size = BLOCK_SIZE;
        // Temperature and max length — clamp to reasonable values
        scalar_t temp = 0.8;
        int max_len = (int) (response_len > 16 ? response_len - 1 : 8);
        organelle_generate(h->organelle, &cfg, prompt ? prompt : "", response_buf, max_len, temp);
        return 0;
    }

    if (!h->loaded_model) {
        const char* msg = "[microgpt-plugin] no model loaded";
        strncpy(response_buf, msg, response_len);
        response_buf[response_len - 1] = '\0';
        return -1;
    }

    // Fallback: echo the prompt with a prefix.
    const char* prefix = "[microgpt] ";
    int written = snprintf(response_buf, response_len, "%s%s", prefix, prompt ? prompt : "");
    if (written < 0) return -1;
    if ((size_t)written >= response_len) response_buf[response_len - 1] = '\0';
    return 0;
}

extern "C" int ai_plugin_generate_text_async(ai_plugin_t handle, const char* prompt) {
    if (!handle || !prompt) return -1;
    MicrogptHandle* h = (MicrogptHandle*)handle;
    if (h->notify_path.empty()) return -1;
    Organelle* org = h->organelle;
    if (!org) return -1;

    std::string notify = h->notify_path;
    std::string promptStr = prompt;

    std::thread t([notify, promptStr, org]() mutable {
        // remove and open notify file for append
        remove(notify.c_str());
        FILE* f = fopen(notify.c_str(), "a");
        if (!f) return;

        MicrogptConfig cfg = microgpt_default_config();
        cfg.n_embd = N_EMBD; cfg.n_head = N_HEAD; cfg.mlp_dim = MLP_DIM; cfg.n_layer = N_LAYER; cfg.block_size = BLOCK_SIZE;
        const int nl = cfg.n_layer;

        // allocate per-layer KV caches
        scalar_t **inf_keys = (scalar_t **)malloc((size_t)nl * sizeof(scalar_t *));
        scalar_t **inf_values = (scalar_t **)malloc((size_t)nl * sizeof(scalar_t *));
        size_t *inf_cache_len = (size_t *)calloc((size_t)nl, sizeof(size_t));
        if (!inf_keys || !inf_values || !inf_cache_len) {
            if (f) fclose(f);
            free(inf_keys); free(inf_values); free(inf_cache_len);
            return;
        }
        for (int l = 0; l < nl; l++) {
            inf_keys[l] = kv_cache_alloc(&cfg);
            inf_values[l] = kv_cache_alloc(&cfg);
            if (!inf_keys[l] || !inf_values[l]) {
                // cleanup
                for (int j = 0; j <= l; j++) { if (inf_keys[j]) kv_cache_free(inf_keys[j]); if (inf_values[j]) kv_cache_free(inf_values[j]); }
                free(inf_keys); free(inf_values); free(inf_cache_len);
                if (f) fclose(f);
                return;
            }
        }

        scalar_t *logits_buf = (scalar_t *)malloc((size_t)cfg.max_vocab * sizeof(scalar_t));
        if (!logits_buf) {
            for (int l = 0; l < nl; l++) { kv_cache_free(inf_keys[l]); kv_cache_free(inf_values[l]); }
            free(inf_keys); free(inf_values); free(inf_cache_len);
            if (f) fclose(f);
            return;
        }

        int pos = 0;
        const Vocab *vocab = &org->vocab;

        // Step 1: feed BOS
        size_t token = vocab->bos_id;
        forward_inference(org->model, token, pos, inf_keys, inf_values, inf_cache_len, logits_buf);
        pos++;

        // Step 2: feed prompt chars
        for (int i = 0; promptStr[i] && pos < cfg.block_size - 1; i++) {
            token = 0;
            for (size_t v = 0; v < vocab->vocab_size; v++) {
                if (vocab->chars[v] == (unsigned char)promptStr[i]) { token = v; break; }
            }
            forward_inference(org->model, token, pos, inf_keys, inf_values, inf_cache_len, logits_buf);
            pos++;
        }

        // Step 3: feed newline separator
        token = 0;
        for (size_t v = 0; v < vocab->vocab_size; v++) { if (vocab->chars[v] == '\n') { token = v; break; } }
        forward_inference(org->model, token, pos, inf_keys, inf_values, inf_cache_len, logits_buf);
        pos++;

        // Step 4: decode token-by-token and stream
        int max_out = cfg.block_size; // limit generated chars to block_size
        for (int g = 0; g < max_out && pos < cfg.block_size; g++) {
            size_t next = sample_token(logits_buf, vocab->vocab_size, cfg.temperature);
            if (next == vocab->bos_id) break;
            unsigned char ch = vocab->chars[next];
            // write byte to notify file
            fwrite(&ch, 1, 1, f);
            fflush(f);
            // stop on newline (terminator)
            if (ch == '\n') break;
            // advance model with this token
            forward_inference(org->model, next, pos, inf_keys, inf_values, inf_cache_len, logits_buf);
            pos++;
            // small sleep to allow consumer to tail
            usleep(50000); // 50ms
        }

        // append end marker
        fwrite("<<STREAM_END>>", 1, strlen("<<STREAM_END>>"), f);
        fflush(f);

        // cleanup
        free(logits_buf);
        for (int l = 0; l < nl; l++) { kv_cache_free(inf_keys[l]); kv_cache_free(inf_values[l]); }
        free(inf_keys); free(inf_values); free(inf_cache_len);
        fclose(f);
    });
    t.detach();
    return 0;
}

extern "C" status_t ai_plugin_list_models(const BMessage* settingsMsg, char* out_buf, size_t out_len) {
    if (!out_buf) return B_BAD_VALUE;
    const char* model_dir = "third_party/microgpt-c/models";
    if (settingsMsg) {
        settingsMsg->FindString("model_dir", &model_dir);
    }
    std::vector<std::string> models = scan_models(model_dir);
    if (models.empty()) {
        models = scan_models("/boot/home/config/non-packaged/add-ons/ai/models");
    }
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

extern "C" int ai_plugin_set_model(ai_plugin_t handle, const char* model_name) {
    if (!handle || !model_name) return -1;
    MicrogptHandle* h = (MicrogptHandle*)handle;
    h->active_model = model_name;

    // Try to locate the checkpoint file in model_dir and attempt to load it.
    std::string candidate = h->model_dir + "/" + model_name;
    if (!file_exists(candidate)) {
        // Maybe model_name is a directory; try a common checkpoint filename inside
        std::string inside = candidate + "/checkpoint.ckpt";
        if (file_exists(inside)) candidate = inside;
        else {
            // Give up — not found
            return -1;
        }
    }

    // Peek header like oql_runtime_load_organelle to read step and vocab
    FILE* f = fopen(candidate.c_str(), "rb");
    if (!f) return -1;
    int header_step = 0;
    size_t header_vocab = 0;
    if (fread(&header_step, sizeof(int), 1, f) != 1 ||
        fread(&header_vocab, sizeof(size_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);

    // Build config using compile-time defaults
    MicrogptConfig cfg = microgpt_default_config();
    cfg.n_embd = N_EMBD;
    cfg.n_head = N_HEAD;
    cfg.mlp_dim = MLP_DIM;
    cfg.n_layer = N_LAYER;
    cfg.block_size = BLOCK_SIZE;

    // Allocate Adam scratch buffers sized similarly to oql_runtime_load_organelle
    const size_t ne = (size_t)cfg.n_embd;
    const size_t bs_ = (size_t)cfg.block_size;
    const size_t md_ = (size_t)cfg.mlp_dim;
    const int nl_ = cfg.n_layer;
    size_t scratch_n = header_vocab * ne * 2   /* wte + lm_head */
                     + bs_ * ne                /* wpe */
                     + (size_t)nl_ * (4 * ne * ne + 2 * md_ * ne)
                     + 1024;                   /* slack */
    scalar_t *m = (scalar_t *)calloc(scratch_n, sizeof(scalar_t));
    scalar_t *v = (scalar_t *)calloc(scratch_n, sizeof(scalar_t));
    if (!m || !v) { free(m); free(v); return -1; }

    int step_out = 0;
    Model* model = checkpoint_load(candidate.c_str(), header_vocab, &cfg, m, v, &step_out);
    free(m); free(v);
    if (!model) {
        return -1;
    }

    // Free previously loaded organelle/model if any
    if (h->organelle) {
        // free minimal organelle: free vocab chars and struct (do not free model here)
        if (h->organelle->vocab.chars) free(h->organelle->vocab.chars);
        free(h->organelle);
        h->organelle = NULL;
    }
    if (h->loaded_model) model_free(h->loaded_model);
    h->loaded_model = model;

    // Create a minimal Organelle
    Organelle* org = (Organelle*)calloc(1, sizeof(Organelle));
    if (!org) return -1;
    org->model = model;
    org->word_level = 0;

    // Attempt to reconstruct the true vocabulary from sidecar files or corpora.
    bool got_vocab = false;
    // Candidate paths to search for corpora / vocab lists
    std::vector<std::string> candidates;
    // base without extension
    std::string base = candidate;
    size_t dot = base.rfind('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    candidates.push_back(base + ".vocab");
    candidates.push_back(base + ".vocab.txt");
    candidates.push_back(base + ".chars");
    candidates.push_back(base + ".chars.txt");
    candidates.push_back(candidate + ".vocab");
    candidates.push_back(candidate + ".txt");
    candidates.push_back(h->model_dir + "/" + h->active_model + ".txt");
    candidates.push_back(h->model_dir + "/" + h->active_model + "/corpus.txt");
    candidates.push_back(h->model_dir + "/" + h->active_model + "/vocab.txt");

    Vocab v = {0};
    Docs docs = {0};
    for (const auto &path : candidates) {
        if (!file_exists(path)) continue;
        // First try treating it as a corpus (load_docs + build_vocab)
        if (load_docs(path.c_str(), &docs, microgpt_default_config().max_docs) == 0 && docs.num_docs > 0) {
            build_vocab(&docs, &v);
            free_docs(&docs);
            // Accept only if vocab size matches header_vocab (checkpoint)
            if (v.vocab_size == header_vocab) {
                org->vocab.vocab_size = v.vocab_size;
                org->vocab.chars = v.chars; // take ownership
                v.chars = NULL;
                org->vocab.bos_id = v.bos_id;
                got_vocab = true;
                break;
            } else {
                // mismatch: free and continue
                if (v.chars) free(v.chars);
            }
        }
        // Next, try reading as a simple char-list (one char per line)
        FILE* vf = fopen(path.c_str(), "rb");
        if (!vf) continue;
        fseek(vf, 0, SEEK_END);
        long vsz = ftell(vf);
        fseek(vf, 0, SEEK_SET);
        if (vsz > 0 && vsz < 65536) {
            // read whole file
            char *buf = (char*)malloc((size_t)vsz + 1);
            if (buf) {
                size_t nread = fread(buf, 1, (size_t)vsz, vf);
                buf[nread] = '\0';
                // collect unique bytes in order of appearance
                unsigned char seen[256] = {0};
                std::vector<unsigned char> chars;
                for (size_t i = 0; i < nread; ++i) {
                    unsigned char c = (unsigned char)buf[i];
                    if (c == '\n' || c == '\r') continue;
                    if (!seen[c]) { seen[c] = 1; chars.push_back(c); }
                }
                free(buf);
                if (!chars.empty() && (chars.size() + 1) == header_vocab) {
                    org->vocab.vocab_size = header_vocab;
                    org->vocab.chars = (unsigned char*)malloc(chars.size());
                    if (org->vocab.chars) {
                        for (size_t i = 0; i < chars.size(); ++i) org->vocab.chars[i] = chars[i];
                        org->vocab.bos_id = (size_t)chars.size();
                        got_vocab = true;
                        fclose(vf);
                        break;
                    }
                }
            }
        }
        fclose(vf);
    }

    if (!got_vocab) {
        // Fallback: synthetic identity mapping (previous behaviour)
        org->vocab.vocab_size = header_vocab;
        org->vocab.chars = (unsigned char*)malloc(header_vocab);
        if (!org->vocab.chars) { free(org); return -1; }
        for (size_t i = 0; i < header_vocab; ++i) org->vocab.chars[i] = (unsigned char)i;
        org->vocab.bos_id = header_vocab ? header_vocab - 1 : 0;
    }

    h->organelle = org;

    return 0;
}


extern "C" const char* get_plugin_name() {
    return "Micro GPT-c";
}
