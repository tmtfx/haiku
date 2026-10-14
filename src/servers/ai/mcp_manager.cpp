/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "mcp_manager.h"
#include <os/ai/AIConfig.h>
#include <Alert.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Node.h>
#include <Path.h>
#include <fs_attr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <sstream>

#ifdef LOVE_IS_IN_THE_AIR
#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlProtocolListener.h>
#include <UrlRequest.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <HttpHeaders.h>
#include <DataIO.h>

using namespace BPrivate::Network;

class LovenseHttpListener : public BUrlProtocolListener {
public:
    LovenseHttpListener() {}
    virtual ~LovenseHttpListener() {}

    bool CertificateVerificationFailed(BUrlRequest* request, 
                                       BCertificate& certificate, 
                                       const char* message) override {
        return false; 
    }
};

BString SendLovenseHttpRequest(const char* host, int32 port, const char* jsonPayload)
{
    if (host == nullptr || host[0] == '\0') host = "127.0.0.1";
    if (port <= 0) port = 20010;

    BString targetUrl;
    targetUrl.SetToFormat("http://%s:%" B_PRId32 "/command", host, port);

    BUrl url(targetUrl.String(), true);
    BMallocIO* outBuffer = new BMallocIO();
    LovenseHttpListener listener;
    BUrlContext context;

    BUrlRequest* req = BUrlProtocolRoster::MakeRequest(url, outBuffer, &listener, &context);
    if (req == nullptr) {
        delete outBuffer;
        return "{\"error\":\"Failed to create HTTP request for Lovense\"}";
    }

    req->SetTimeout(5000000); // 5s timeout

    BHttpRequest* http = dynamic_cast<BHttpRequest*>(req);
    if (http != nullptr) {
        http->SetMethod(B_HTTP_POST);

        if (jsonPayload != nullptr && strlen(jsonPayload) > 0) {
            BMallocIO* inBuffer = new BMallocIO();
            inBuffer->WriteExactly(jsonPayload, strlen(jsonPayload));
            http->AdoptInputData(inBuffer, strlen(jsonPayload));
        }

        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "application/json");
        http->SetHeaders(headers);
    }

    thread_id thread = req->Run();
    status_t rc = B_ERROR;
    if (thread >= 0) {
        wait_for_thread(thread, &rc);
    } else {
        rc = thread;
    }

    const void* rawBuf = outBuffer->Buffer();
    size_t len = outBuffer->BufferLength();
    BString response;

    if (rawBuf != nullptr && len > 0) {
        response.SetTo((const char*)rawBuf, len);
    } else {
        response = "{\"error\":\"Empty response from Lovense Connect or device unreachable.\"}";
    }

    delete req;
    delete outBuffer;

    return response;
}

static int32 ExtractIntArg(const BMessage& args, const char* name, int32 defaultVal = 0)
{
    int32 val32;
    if (args.FindInt32(name, &val32) == B_OK) return val32;
    int64 val64;
    if (args.FindInt64(name, &val64) == B_OK) return (int32)val64;
    double vald;
    if (args.FindDouble(name, &vald) == B_OK) return (int32)vald;
    const char* str = args.FindString(name);
    if (str != nullptr && strlen(str) > 0) return atoi(str);
    return defaultVal;
}

static BString ExtractStrArg(const BMessage& args, const char* name, const char* defaultVal = "")
{
    const char* str = args.FindString(name);
    if (str != nullptr) return BString(str);
    return BString(defaultVal);
}
#endif // LOVE_IS_IN_THE_AIR


BString ExtractExecutable(const char* fullCmd) {
    if (!fullCmd || strlen(fullCmd) == 0)
        return "";

    std::stringstream ss(fullCmd);
    std::string token;

    while (ss >> token) {
        // Ignora eventuali assegnazioni di variabili d'ambiente tipo VAR=valore
        if (token.find('=') != std::string::npos)
            continue;
            
        // Estrai solo il nome del file se c'è un percorso assoluto (es. /bin/ls -> ls)
        size_t lastSlash = token.find_last_of('/');
        if (lastSlash != std::string::npos) {
            token = token.substr(lastSlash + 1);
        }
        
        return BString(token.c_str());
    }

    return "";
}
BString JsonEscape(const char* data, ssize_t length)
{
    if (data == nullptr || length <= 0)
        return "\"\"";

    BString output("\""); // Apre con le virgolette JSON

    for (ssize_t i = 0; i < length; i++) {
        unsigned char c = static_cast<unsigned char>(data[i]);

        switch (c) {
            case '\"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b";  break;
            case '\f': output << "\\f";  break;
            case '\n': output << "\\n";  break;
            case '\r': output << "\\r";  break;
            case '\t': output << "\\t";  break;
            default:
                if (c < 0x20) {
                    // Gestisce tutti i caratteri di controllo di basso livello (inclusi \0 intermedi)
                    char hexBuf[8];
                    snprintf(hexBuf, sizeof(hexBuf), "\\u%04x", c);
                    output << hexBuf;
                } else {
                    output << static_cast<char>(c);
                }
                break;
        }
    }

    output << "\""; // Chiude con le virgolette JSON
    return output;
}
bool ExtractStringFromJson(const char* json, const char* key, BString& out, bool unescapeControlChars) {
    if (!json || !key) return false;
    BString needle;
    needle.SetToFormat("\"%s\"", key);
    int32 pos = BString(json).FindFirst(needle);
    if (pos == B_ERROR) return false;
    
    const char* p = json + pos + needle.Length();
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p != '"') return false;
    p++; // salta le virgolette aperte
    
    const char* q = p;
    bool escaped = false;
    while (*q) {
        if (escaped) {
            escaped = false;
        } else if (*q == '\\') {
            escaped = true;
        } else if (*q == '"') {
            break;
        }
        q++;
    }
    
    out.SetTo(p, q - p);
    
    if (unescapeControlChars) {
        out.ReplaceAll("\\n", "\n");
        out.ReplaceAll("\\t", "\t");
        out.ReplaceAll("\\r", "\r");
    }
    
    out.ReplaceAll("\\\\", "\\");
    out.ReplaceAll("\\\"", "\"");
        
    out.ReplaceAll("\\u003c", "<");
    out.ReplaceAll("\\u003e", ">");
    out.ReplaceAll("\\u0026", "&");
    out.ReplaceAll("\\u0027", "'");
    out.ReplaceAll("\\u003d", "=");
    
    out.ReplaceAll("\\u201c", "\"");
    out.ReplaceAll("\\u201d", "\"");
    out.ReplaceAll("\\u2018", "'");
    out.ReplaceAll("\\u2019", "'");
    
    return true;
}

BString RunSystemCommand(const char* command) {
    char buffer[128];
    BString result = "";
    FILE* pipe = popen(command, "r");
    if (!pipe) return "Errore: impossibile avviare il comando.";
    
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != NULL)
            result << buffer;
    }
    pclose(pipe);
    return result;
}

status_t RemoveEntryRecursive(BEntry* entry) 
{
    if (entry == nullptr || !entry->Exists())
        return B_ENTRY_NOT_FOUND;

    if (!entry->IsDirectory()) {
        // È un file semplice o un symlink: rimozione diretta
        return entry->Remove();
    }

    // Se è una directory, svuotiamo prima il suo contenuto
    BDirectory dir(entry);
    if (dir.InitCheck() != B_OK)
        return dir.InitCheck();

    BEntry childEntry;
    while (dir.GetNextEntry(&childEntry, true) == B_OK) {
        status_t err = RemoveEntryRecursive(&childEntry);
        if (err != B_OK)
            return err; // Interrompe se non riesce a cancellare un elemento interno
    }

    // Ora che la directory è vuota, possiamo rimuoverla
    return entry->Remove();
}

// Funzione ausiliaria per la gestione dei permessi interattivi (ASK)
static bool ConfirmActionWithUser(const char* actionDescription, const char* targetPath) {
    BString message;
    message.SetToFormat("L'assistente AI ha richiesto di eseguire la seguente azione:\n\n"
                        "• Azione: %s\n"
                        "• Target: %s\n\n"
                        "Vuoi autorizzare questa operazione?", 
                        actionDescription, targetPath ? targetPath : "N/D");

    BAlert* alert = new BAlert("Permesso MCP", message.String(), 
                               "Nega", "Autorizza", nullptr, 
                               B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    alert->SetShortcut(0, B_ESCAPE);
    
    return (alert->Go() == 1);
}

int32 ConfirmTerminalCommandWithUser(ClientSession* session, const char* cmdStr, const BString& exeName) {
    BString alertText;
    alertText.SetToFormat(
        "L'assistente AI richiede l'esecuzione del seguente comando da terminale:\n\n"
        "  \"%s\"\n\n"
        "Eseguibile individuato: '%s'\n\n"
        "Come desideri procedere?",
        cmdStr,
        exeName.String()
    );

    // BAlert con i 3 pulsanti richiesti:
    // Pulsante 0: "Rifiuta" (Default / ESC)
    // Pulsante 1: "Esegui una volta"
    // Pulsante 2: "Sempre per '%s'" (Aggiunge l'eseguibile in whitelist)
    
    BString alwaysLabel;
    alwaysLabel.SetToFormat("Sempre per '%s'", exeName.String());

    BAlert* alert = new BAlert(
        "Sicurezza MCP - Comando Terminale",
        alertText.String(),
        "Rifiuta",
        "Esegui una volta",
        alwaysLabel.String(),
        B_WIDTH_AS_USUAL,
        B_WARNING_ALERT
    );

    alert->SetShortcut(0, B_ESCAPE);

    return alert->Go(); // Ritorna 0, 1 o 2
}

BString EscapeShellArg(const BString& input) {
    BString escaped = input;
    // In POSIX shell, per inserire ' dentro una stringa racchiusa da ' ', si usa '\''
    escaped.ReplaceAll("'", "'\\''");
    BString result;
    result.SetToFormat("'%s'", escaped.String());
    return result;
}

BString ExecuteLocalTool(ClientSession* session, const char* tool_name, const BMessage& arguments) {
	if (session == nullptr) {
        return "Error: Invalid session.";
    }
    BString name(tool_name);
    BString result = "";
    
    uint32 permissions = session->mcp_permissions;
    
    uint32 readPerm = permissions & AI_PERM_READ_MASK;
    uint32 writePerm = permissions & AI_PERM_WRITE_MASK;

    // --- SYSTEM INFO ---
    if (name == "get_system_stats") {
        if (!(permissions & AI_PERM_SYSTEM_INFO))
            return "{\"error\":\"Permission denied: System Info access is disabled.\"}";

        result << "=== SYSTEM UPTIME ===\n" << RunSystemCommand("uptime") << "\n";
        result << "=== DISK SPACE ===\n" << RunSystemCommand("df -h") << "\n";
        result << "=== MEMORY ===\n" << RunSystemCommand("sysinfo -mem") << "\n";
    } 
    else if (name == "show_alert_dialog") {
        if (!(permissions & AI_PERM_SYSTEM_INFO))
            return "{\"error\":\"Permission denied: System Info access is disabled.\"}";

        BString text = arguments.FindString("text");
        BString title = arguments.FindString("title");
        if (text.IsEmpty()) text = "Messaggio vuoto";
        if (title.IsEmpty()) title = "Alert";
        
        // Rimuoviamo text.ReplaceAll("\"", "\\\""); e usiamo l'helper per rendere sicuro il testo:
        BString safeText = EscapeShellArg(text);

        BString cmd;
        // NOTA: %s non è racchiuso da virgolette perché EscapeShellArg include già gli apici singoli!
        cmd.SetToFormat("alert --info %s", safeText.String());

        result = RunSystemCommand(cmd.String());
    }

    // --- READ ACCESS TOOLS ---
    else if (name == "list_directory") {
        if (readPerm == AI_PERM_READ_NO)
            return "{\"error\":\"Permission denied: File Read access is disabled.\"}";

        BString path = arguments.FindString("path");
        if (path.IsEmpty()) path = "/boot/home";

        if (readPerm == AI_PERM_READ_ASK && !ConfirmActionWithUser("Elenco directory", path.String()))
            return "{\"error\":\"User denied permission to list directory.\"}";

        BString cmd;
        cmd.SetToFormat("ls -la \"%s\"", path.String());
        result = RunSystemCommand(cmd.String());
    }
    else if (name == "read_file") {
        if (readPerm == AI_PERM_READ_NO)
            return "{\"error\":\"Permission denied: File Read access is disabled.\"}";

        BString path = arguments.FindString("path");
        if (path.IsEmpty()) return "{\"error\":\"Missing path argument\"}";

        if (readPerm == AI_PERM_READ_ASK && !ConfirmActionWithUser("Lettura file", path.String()))
            return "{\"error\":\"User denied permission to read file.\"}";

        BFile file(path.String(), B_READ_ONLY);
        if (file.InitCheck() != B_OK) {
            result.SetToFormat("{\"error\":\"Cannot open file: %s\"}", strerror(file.InitCheck()));
        } else {
            off_t size = 0;
            file.GetSize(&size);
            if (size > 1024 * 1024) { // Limite 1MB per sicurezza
                return "{\"error\":\"File too large to read (max 1MB)\"}";
            }

            std::vector<char> buffer(size);
            ssize_t bytesRead = file.Read(buffer.data(), size);
            if (bytesRead >= 0) {
                // JsonEscape gestisce sia i dati, sia la lunghezza, sia le virgolette JSON di contenimento
                BString escapedContent = JsonEscape(buffer.data(), bytesRead);
                
                // NOTA: escapedContent include già le virgolette racchiudenti!
                result.SetToFormat("{\"path\":\"%s\",\"bytes\":%ld,\"content\":%s}",
                    path.String(), (long)bytesRead, escapedContent.String());
            } else {
                result.SetToFormat("{\"error\":\"Read error: %s\"}", strerror(bytesRead));
            }
        }
    }
    else if (name == "search_text") {
        if (readPerm == AI_PERM_READ_NO)
            return "{\"error\":\"Permission denied: File Read access is disabled.\"}";

        BString pattern = arguments.FindString("pattern");
        BString path = arguments.FindString("path");
        if (pattern.IsEmpty()) return "{\"error\":\"Missing pattern argument\"}";
        if (path.IsEmpty()) path = "/boot/home";

        if (readPerm == AI_PERM_READ_ASK && !ConfirmActionWithUser("Ricerca testo nel file system", path.String()))
            return "{\"error\":\"User denied permission to search text.\"}";

        pattern.ReplaceAll("\"", "\\\"");
        BString cmd;
        cmd.SetToFormat("grep -rn \"%s\" \"%s\"", pattern.String(), path.String());
        result = RunSystemCommand(cmd.String());
    }

    // --- WRITE ACCESS TOOLS ---
    else if (name == "create_file") {
        if (writePerm == AI_PERM_WRITE_NO)
            return "{\"error\":\"Permission denied: File Write access is disabled.\"}";

        BString path = arguments.FindString("path");
        BString content = arguments.FindString("content");
        if (path.IsEmpty()) return "{\"error\":\"Missing path argument\"}";

        if (writePerm == AI_PERM_WRITE_ASK && !ConfirmActionWithUser("Creazione/Scrittura file", path.String()))
            return "{\"error\":\"User denied permission to create file.\"}";

        BFile file(path.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
        if (file.InitCheck() != B_OK) {
            result.SetToFormat("{\"error\":\"Cannot create file: %s\"}", strerror(file.InitCheck()));
        } else {
            ssize_t bytesWritten = file.Write(content.String(), content.Length());
            if (bytesWritten >= 0) {
                result.SetToFormat("File created successfully. Written %" B_PRId32 " bytes.", (int32)bytesWritten);
            } else {
                result.SetToFormat("{\"error\":\"Write error: %s\"}", strerror(bytesWritten));
            }
        }
    }
    else if (name == "make_directory") {
        if (writePerm == AI_PERM_WRITE_NO)
            return "{\"error\":\"Permission denied: File Write access is disabled.\"}";

        BString path = arguments.FindString("path");
        if (path.IsEmpty()) return "{\"error\":\"Missing path argument\"}";

        if (writePerm == AI_PERM_WRITE_ASK && !ConfirmActionWithUser("Creazione cartella", path.String()))
            return "{\"error\":\"User denied permission to create directory.\"}";

        status_t err = create_directory(path.String(), 0755);
        if (err == B_OK) {
            result = "Directory created successfully.";
        } else {
            result.SetToFormat("{\"error\":\"Failed to create directory: %s\"}", strerror(err));
        }
    }
    else if (name == "delete_file") {
        if (writePerm == AI_PERM_WRITE_NO)
            return "{\"error\":\"Permission denied: File Write access is disabled.\"}";

        BString path = arguments.FindString("path");
        if (path.IsEmpty()) return "{\"error\":\"Missing path argument\"}";

        if (writePerm == AI_PERM_WRITE_ASK && !ConfirmActionWithUser("Eliminazione file/cartella", path.String()))
            return "{\"error\":\"User denied permission to delete file.\"}";

        BEntry entry(path.String());
        if (!entry.Exists()) {
            return "{\"error\":\"File or directory does not exist\"}";
        }

        status_t err = RemoveEntryRecursive(&entry);
        if (err == B_OK) {
            result = "Deleted successfully.";
        } else {
           result.SetToFormat("{\"error\":\"Failed to delete: %s\"}", strerror(err));
        }
    }
    else if (name == "open_document") {
        if (readPerm == AI_PERM_READ_NO && writePerm == AI_PERM_WRITE_NO)
            return "{\"error\":\"Permission denied: File Access is disabled.\"}";

        BString path = arguments.FindString("path");
        if (path.IsEmpty()) return "{\"error\":\"Missing path argument\"}";

        if ((readPerm == AI_PERM_READ_ASK || writePerm == AI_PERM_WRITE_ASK) && 
            !ConfirmActionWithUser("Apertura documento/applicazione", path.String())) {
            return "{\"error\":\"User denied permission to open document.\"}";
        }

        BString cmd;
        cmd.SetToFormat("open \"%s\"", path.String());
        result = RunSystemCommand(cmd.String());
    }

    // --- MANAGING BFS ATTRIBUTES (READ OR WRITE) ---
    else if (name == "manage_attribute") {
        BString action = arguments.FindString("action"); // "read", "write", "list"
        BString path = arguments.FindString("path");
        BString attrName = arguments.FindString("name");
        BString attrValue = arguments.FindString("value");

        if (path.IsEmpty() || action.IsEmpty()) {
            return "{\"error\":\"Missing action or path arguments\"}";
        }

        if (action == "write") {
            if (writePerm == AI_PERM_WRITE_NO)
                return "{\"error\":\"Permission denied: File Write access is disabled.\"}";

            if (writePerm == AI_PERM_WRITE_ASK && !ConfirmActionWithUser("Scrittura attributo BFS", path.String()))
                return "{\"error\":\"User denied permission to write BFS attribute.\"}";
        } else { // "read" or "list"
            if (readPerm == AI_PERM_READ_NO)
                return "{\"error\":\"Permission denied: File Read access is disabled.\"}";

            if (readPerm == AI_PERM_READ_ASK && !ConfirmActionWithUser("Lettura attributi BFS", path.String()))
                return "{\"error\":\"User denied permission to read BFS attributes.\"}";
        }

        BNode node(path.String());
        if (node.InitCheck() != B_OK) {
            result.SetToFormat("{\"error\":\"Cannot open node: %s\"}", strerror(node.InitCheck()));
            return result;
        }

        if (action == "list") {
            char nameBuf[B_ATTR_NAME_LENGTH];
            node.RewindAttrs();
            result << "=== BFS ATTRIBUTES ===\n";
            while (node.GetNextAttrName(nameBuf) == B_OK) {
                attr_info info;
                if (node.GetAttrInfo(nameBuf, &info) == B_OK) {
                    result << nameBuf << " (type: " << (int32)info.type << ", size: " << info.size << " bytes)\n";
                } else {
                    result << nameBuf << "\n";
                }
            }
        }
        else if (action == "read") {
            if (attrName.IsEmpty()) return "{\"error\":\"Missing attribute name for read action\"}";
            attr_info info;
            if (node.GetAttrInfo(attrName.String(), &info) != B_OK) {
                result.SetToFormat("{\"error\":\"Attribute %s not found\"}", attrName.String());
            } else {
                std::vector<char> buffer(info.size + 1);
                ssize_t bytesRead = node.ReadAttr(attrName.String(), info.type, 0, buffer.data(), info.size);
                if (bytesRead >= 0) {
                    buffer[bytesRead] = '\0';
                    result = buffer.data();
                } else {
                    result.SetToFormat("{\"error\":\"Failed to read attribute: %s\"}", strerror(bytesRead));
                }
            }
        }
        else if (action == "write") {
            if (attrName.IsEmpty() || attrValue.IsEmpty()) return "{\"error\":\"Missing attribute name or value for write action\"}";
            ssize_t bytesWritten = node.WriteAttr(attrName.String(), B_STRING_TYPE, 0, attrValue.String(), attrValue.Length() + 1);
            if (bytesWritten >= 0) {
                result = "Attribute written successfully.";
            } else {
                result.SetToFormat("{\"error\":\"Failed to write attribute: %s\"}", strerror(bytesWritten));
            }
        }
        else {
            result = "{\"error\":\"Invalid action. Supported: list, read, write\"}";
        }
    }

    // --- TERMINAL COMMANDS --- 
    /*
    else if (name == "run_terminal_command") {
        if (!(permissions & AI_PERM_RUN_COMMANDS))
            return "{\"error\":\"Permission denied: Terminal Command execution is disabled.\"}";

        BString cmd = arguments.FindString("cmd");
        if (cmd.IsEmpty()) return "{\"error\":\"Missing cmd argument\"}";

        if (!ConfirmActionWithUser("Esecuzione comando terminale", cmd.String()))
            return "{\"error\":\"User denied permission to execute terminal command.\"}";

        result = RunSystemCommand(cmd.String());
    }*/
    else if (name == "run_terminal_command") {
		if (!(permissions & AI_PERM_RUN_COMMANDS))
			return "{\"error\":\"Permission denied: Terminal Command execution is disabled.\"}";

		BString cmd = arguments.FindString("cmd");
		if (cmd.IsEmpty()) 
			return "{\"error\":\"Missing cmd argument\"}";

		// 1. Estraiamo il nome dell'eseguibile (es. "git", "ls", "pkgman")
		BString exeName = ExtractExecutable(cmd.String());

		// 2. Verifichiamo se l'eseguibile è già presente nella lista dei consentiti per questa sessione
		bool isAlwaysAllowed = false;
		if (session && !exeName.IsEmpty()) {
			isAlwaysAllowed = (session->allowed_executables.find(exeName) != session->allowed_executables.end());
		}

		if (!isAlwaysAllowed) {
			// 3. Chiediamo all'utente tramite BAlert
			int32 choice = ConfirmTerminalCommandWithUser(session, cmd.String(), exeName);

			if (choice == 0) {
				// Rifiuta
				return "{\"error\":\"User denied permission to execute terminal command.\"}";
			} 
			else if (choice == 2) {
				// "Sempre per questo eseguibile" -> Salva nella sessione
				if (session && !exeName.IsEmpty()) {
					session->allowed_executables.insert(exeName);
					fprintf(stderr, "[MCP_MANAGER] Aggiunto '%s' alla whitelist di sessione.\n", exeName.String());
				}
			}
			// choice == 1 ("Esegui una volta") prosegue normalmente senza salvare
		} else {
			fprintf(stderr, "[MCP_MANAGER] Comando '%s' (exe: '%s') eseguito automaticamente per whitelist di sessione.\n", 
					cmd.String(), exeName.String());
		}

		// 4. Esecuzione del comando
		result = RunSystemCommand(cmd.String());
	}

#ifdef LOVE_IS_IN_THE_AIR
    // --- LOVENSE TOOLS ---
    else if (name == "lovense_get_toys") {
        if (!(permissions & AI_PERM_LOVENSE))
            return "{\"error\":\"Permission denied: Lovense access is disabled in AI settings.\"}";

        AISettings settings;
        LoadAISettings(settings);

        BString host = ExtractStrArg(arguments, "host");
        if (host.IsEmpty()) host = settings.lovense_host.IsEmpty() ? "127.0.0.1" : settings.lovense_host;

        int32 port = ExtractIntArg(arguments, "port", settings.lovense_port > 0 ? settings.lovense_port : 20010);

        BString payload = "{\"command\":\"GetToys\"}";
        result = SendLovenseHttpRequest(host.String(), port, payload.String());
    }
    else if (name == "lovense_control") {
        if (!(permissions & AI_PERM_LOVENSE))
            return "{\"error\":\"Permission denied: Lovense access is disabled in AI settings.\"}";

        AISettings settings;
        LoadAISettings(settings);

        BString host = ExtractStrArg(arguments, "host");
        if (host.IsEmpty()) host = settings.lovense_host.IsEmpty() ? "127.0.0.1" : settings.lovense_host;

        int32 port = ExtractIntArg(arguments, "port", settings.lovense_port > 0 ? settings.lovense_port : 20010);

        BString action = ExtractStrArg(arguments, "action");
        if (action.IsEmpty()) action = ExtractStrArg(arguments, "act");
        if (action.IsEmpty()) return "{\"error\":\"Missing action argument (e.g. Vibrate, Rotate, Pump, Stop)\"}";

        int32 intensity = ExtractIntArg(arguments, "intensity", 0);
        int32 duration = ExtractIntArg(arguments, "duration", 0);
        if (duration < 0) duration = 0;

        BString toyId = ExtractStrArg(arguments, "toy_id");
        if (toyId.IsEmpty()) toyId = ExtractStrArg(arguments, "toy");

        BString payload;
        if (action.ICompare("Stop") == 0) {
            if (toyId.IsEmpty()) {
                payload = "{\"command\":\"Function\",\"action\":\"Stop\",\"timeSec\":0,\"apiVer\":1}";
            } else {
                payload.SetToFormat("{\"command\":\"Function\",\"action\":\"Stop\",\"timeSec\":0,\"toy\":\"%s\",\"apiVer\":1}",
                    toyId.String());
            }
        } else {
            BString actCmd = action;
            if (actCmd.FindFirst(':') == B_ERROR) {
                if (actCmd.ICompare("Pump") == 0) {
                    if (intensity < 0) intensity = 0;
                    if (intensity > 3) intensity = 3;
                } else if (actCmd.ICompare("Preset") == 0) {
                    if (intensity < 1) intensity = 1;
                    if (intensity > 3) intensity = 3;
                } else {
                    if (intensity < 0) intensity = 0;
                    if (intensity > 20) intensity = 20;
                }
                actCmd.SetToFormat("%s:%" B_PRId32, action.String(), intensity);
            }

            if (toyId.IsEmpty()) {
                payload.SetToFormat("{\"command\":\"Function\",\"action\":\"%s\",\"timeSec\":%" B_PRId32 ",\"apiVer\":1}",
                    actCmd.String(), duration);
            } else {
                payload.SetToFormat("{\"command\":\"Function\",\"action\":\"%s\",\"timeSec\":%" B_PRId32 ",\"toy\":\"%s\",\"apiVer\":1}",
                    actCmd.String(), duration, toyId.String());
            }
        }

        result = SendLovenseHttpRequest(host.String(), port, payload.String());
    }
    else if (name == "lovense_stop_all") {
        if (!(permissions & AI_PERM_LOVENSE))
            return "{\"error\":\"Permission denied: Lovense access is disabled in AI settings.\"}";

        AISettings settings;
        LoadAISettings(settings);

        BString host = ExtractStrArg(arguments, "host");
        if (host.IsEmpty()) host = settings.lovense_host.IsEmpty() ? "127.0.0.1" : settings.lovense_host;

        int32 port = ExtractIntArg(arguments, "port", settings.lovense_port > 0 ? settings.lovense_port : 20010);

        BString payload = "{\"command\":\"Function\",\"action\":\"Stop\",\"timeSec\":0,\"apiVer\":1}";
        result = SendLovenseHttpRequest(host.String(), port, payload.String());
    }
#endif // LOVE_IS_IN_THE_AIR
    else {
        result.SetToFormat("{\"error\":\"Unknown tool: %s\"}", tool_name);
    }

    return result;
}

static BMessage* CreateToolMessage(const char* name, const char* desc, BMessage* properties, const char* requiredField = nullptr) {
    BMessage* tool = new BMessage();
    tool->AddString("name", name);
    tool->AddString("description", desc);
    tool->AddInt32("exec_type", 1);
    tool->AddString("exec_target", "");

    BMessage parameters;
    parameters.AddString("type", "object");
    if (properties) {
        parameters.AddMessage("properties", properties);
    } else {
        BMessage emptyProps;
        parameters.AddMessage("properties", &emptyProps);
    }
    if (requiredField) {
        parameters.AddString("required", requiredField);
    }
    tool->AddMessage("parameters", &parameters);
    return tool;
}

void PopulateMcpTools(BList& mpcManager, uint32 permissions) {
    for (int32 i = 0; i < mpcManager.CountItems(); i++) {
        delete (BMessage*)mpcManager.ItemAt(i);
    }
    mpcManager.MakeEmpty();

    uint32 readPerm = permissions & AI_PERM_READ_MASK;
    uint32 writePerm = permissions & AI_PERM_WRITE_MASK;

    // 1. Area Info Sistema (AI_PERM_SYSTEM_INFO)
    if (permissions & AI_PERM_SYSTEM_INFO) {
        mpcManager.AddItem(CreateToolMessage("get_system_stats", 
            "Get real-time CPU statistics, memory statistics, disk space, and system uptime.", nullptr));

        BMessage properties;
        BMessage textProp;
        textProp.AddString("type", "string");
        textProp.AddString("description", "The message text to display in the dialog box.");
        properties.AddMessage("text", &textProp);
        
        BMessage titleProp;
        titleProp.AddString("type", "string");
        titleProp.AddString("description", "The title of the dialog box window.");
        properties.AddMessage("title", &titleProp);
        
        mpcManager.AddItem(CreateToolMessage("show_alert_dialog", 
            "Display a native Haiku BAlert dialog box on the screen with custom text and title.", &properties, "text"));
    }

    // 2. Area File System - READ (ai_mcp_perm_read != NO)
    if (readPerm != AI_PERM_READ_NO) {
        {
            BMessage properties;
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The folder path to list (e.g. /boot/home).");
            properties.AddMessage("path", &pathProp);
            mpcManager.AddItem(CreateToolMessage("list_directory", 
                "List all files and subdirectories within a given directory path.", &properties, "path"));
        }
        {
            BMessage properties;
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The absolute path of the file to read (e.g. /boot/home/ReadMe.txt).");
            properties.AddMessage("path", &pathProp);
            mpcManager.AddItem(CreateToolMessage("read_file", 
                "Read the full text content of a specified file on disk.", &properties, "path"));
        }
        {
            BMessage properties;
            BMessage patternProp;
            patternProp.AddString("type", "string");
            patternProp.AddString("description", "The regex or text pattern to search for.");
            properties.AddMessage("pattern", &patternProp);
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The directory path to search within (defaults to /boot/home).");
            properties.AddMessage("path", &pathProp);
            mpcManager.AddItem(CreateToolMessage("search_text", 
                "Search for regular expression patterns inside file contents within a directory (like grep).", &properties, "pattern"));
        }
    }

    // 3. Area File System - WRITE (ai_mcp_perm_write != NO)
    if (writePerm != AI_PERM_WRITE_NO) {
        {
            BMessage properties;
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The absolute path of the file to create (e.g. /boot/home/new_file.txt).");
            properties.AddMessage("path", &pathProp);
            BMessage contentProp;
            contentProp.AddString("type", "string");
            contentProp.AddString("description", "The text content to write into the file.");
            properties.AddMessage("content", &contentProp);
            mpcManager.AddItem(CreateToolMessage("create_file", 
                "Create a new text file (or overwrite an existing one) with the specified text content.", &properties, "path"));
        }
        {
            BMessage properties;
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The folder path to create (e.g. /boot/home/my_folder).");
            properties.AddMessage("path", &pathProp);
            mpcManager.AddItem(CreateToolMessage("make_directory", 
                "Create a new directory or folder at the specified path.", &properties, "path"));
        }
        {
            BMessage properties;
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The absolute path of the file or directory to delete.");
            properties.AddMessage("path", &pathProp);
            mpcManager.AddItem(CreateToolMessage("delete_file", 
                "Delete or remove a file or directory from the disk.", &properties, "path"));
        }
    }

    // 4. Area File System - SHARED (READ O WRITE attivi)
    if (readPerm != AI_PERM_READ_NO || writePerm != AI_PERM_WRITE_NO) {
        {
            BMessage properties;
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The absolute path of the file, image, application or document to open.");
            properties.AddMessage("path", &pathProp);
            mpcManager.AddItem(CreateToolMessage("open_document", 
                "Open any file, image, document, or application using default Haiku application.", &properties, "path"));
        }
        {
            BMessage properties;
            BMessage actionProp;
            actionProp.AddString("type", "string");
            actionProp.AddString("description", "The action to perform: 'read', 'write', or 'list'.");
            properties.AddMessage("action", &actionProp);
            BMessage pathProp;
            pathProp.AddString("type", "string");
            pathProp.AddString("description", "The absolute file path.");
            properties.AddMessage("path", &pathProp);
            BMessage nameProp;
            nameProp.AddString("type", "string");
            nameProp.AddString("description", "The attribute name (e.g. AI:plugin_type) (required for read and write).");
            properties.AddMessage("name", &nameProp);
            BMessage valueProp;
            valueProp.AddString("type", "string");
            valueProp.AddString("description", "The string value to write (required for write).");
            properties.AddMessage("value", &valueProp);
            mpcManager.AddItem(CreateToolMessage("manage_attribute", 
                "Read, write, or list BFS custom file attributes on a file in Haiku.", &properties, "action"));
        }
    }

    // 5. Area Comandi Terminale (AI_PERM_RUN_COMMANDS)
    if (permissions & AI_PERM_RUN_COMMANDS) {
        BMessage properties;
        BMessage cmdProp;
        cmdProp.AddString("type", "string");
        cmdProp.AddString("description", "The full terminal command string to execute.");
        properties.AddMessage("cmd", &cmdProp);
        mpcManager.AddItem(CreateToolMessage("run_terminal_command", 
            "Run any arbitrary terminal shell command under the current user context. WARNING: Use with extreme caution.", &properties, "cmd"));
    }

#ifdef LOVE_IS_IN_THE_AIR
    // 6. Area Lovense (AI_PERM_LOVENSE)
    if (permissions & AI_PERM_LOVENSE) {
        {
            BMessage properties;
            BMessage hostProp;
            hostProp.AddString("type", "string");
            hostProp.AddString("description", "Optional IP address of Lovense Connect (defaults to configured host).");
            properties.AddMessage("host", &hostProp);
            BMessage portProp;
            portProp.AddString("type", "integer");
            portProp.AddString("description", "Optional HTTP port of Lovense Connect (defaults to 20010).");
            properties.AddMessage("port", &portProp);

            mpcManager.AddItem(CreateToolMessage("lovense_get_toys", 
                "Get the list of all connected Lovense sexual toys/devices via Lovense Connect (returns toy IDs, names, status, and battery percentage).", 
                &properties));
        }
        {
            BMessage properties;
            BMessage actionProp;
            actionProp.AddString("type", "string");
            actionProp.AddString("description", "Action to perform: 'Vibrate', 'Rotate', 'Pump', 'Thrusting', 'Fingering', 'Suction', 'Preset', or 'Stop'.");
            properties.AddMessage("action", &actionProp);

            BMessage intensityProp;
            intensityProp.AddString("type", "integer");
            intensityProp.AddString("description", "Intensity level from 0 to 20 (0 stops; for Pump 0-3, for Preset 1-3). Default is 0.");
            properties.AddMessage("intensity", &intensityProp);

            BMessage durationProp;
            durationProp.AddString("type", "integer");
            durationProp.AddString("description", "Duration in seconds (0 = continuous until stopped). Default is 0.");
            properties.AddMessage("duration", &durationProp);

            BMessage toyIdProp;
            toyIdProp.AddString("type", "string");
            toyIdProp.AddString("description", "Optional specific toy ID. If omitted, applies to all connected toys.");
            properties.AddMessage("toy_id", &toyIdProp);

            BMessage hostProp;
            hostProp.AddString("type", "string");
            hostProp.AddString("description", "Optional IP address of Lovense Connect.");
            properties.AddMessage("host", &hostProp);

            BMessage portProp;
            portProp.AddString("type", "integer");
            portProp.AddString("description", "Optional HTTP port of Lovense Connect.");
            properties.AddMessage("port", &portProp);

            mpcManager.AddItem(CreateToolMessage("lovense_control", 
                "Send a vibration, rotation, thrusting, pumping, suction, or stop command to connected Lovense toys.", 
                &properties, "action"));
        }
        {
            BMessage properties;
            BMessage hostProp;
            hostProp.AddString("type", "string");
            hostProp.AddString("description", "Optional IP address of Lovense Connect.");
            properties.AddMessage("host", &hostProp);
            BMessage portProp;
            portProp.AddString("type", "integer");
            portProp.AddString("description", "Optional HTTP port of Lovense Connect.");
            properties.AddMessage("port", &portProp);

            mpcManager.AddItem(CreateToolMessage("lovense_stop_all", 
                "Immediately stop all vibrations, rotations, and movements on all connected Lovense toys.", 
                &properties));
        }
    }
#endif // LOVE_IS_IN_THE_AIR
}
