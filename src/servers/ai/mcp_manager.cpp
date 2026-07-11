/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include "mcp_manager.h"
#include <os/ai/AIConfig.h>
#include <File.h>
#include <Directory.h>
#include <Entry.h>
#include <Node.h>
#include <Path.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static bool ExtractStringFromJson(const char* json, const char* key, BString& out) {
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
	// Unescape base
	out.ReplaceAll("\\\\", "\\");
	out.ReplaceAll("\\\"", "\"");
	out.ReplaceAll("\\n", "\n");
	out.ReplaceAll("\\t", "\t");
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

BString ExecuteLocalTool(const char* tool_name, const BMessage& arguments) {
	BString name(tool_name);
	BString result = "";

	if (name == "get_system_stats") {
		result << "=== SYSTEM UPTIME ===\n" << RunSystemCommand("uptime") << "\n";
		result << "=== DISK SPACE ===\n" << RunSystemCommand("df -h") << "\n";
		result << "=== MEMORY ===\n" << RunSystemCommand("free -h") << "\n";
	} 
	else if (name == "show_alert_dialog") {
		BString text = arguments.FindString("text");
		BString title = arguments.FindString("title");
		if (text.IsEmpty()) text = "Messaggio vuoto";
		if (title.IsEmpty()) title = "Alert";
		
		text.ReplaceAll("\"", "\\\"");
		BString cmd;
		cmd.SetToFormat("alert --info \"%s\"", text.String());
		result = RunSystemCommand(cmd.String());
	}
	else if (name == "list_directory") {
		BString path = arguments.FindString("path");
		if (path.IsEmpty()) path = "/boot/home";
		
		BString cmd;
		cmd.SetToFormat("ls -la \"%s\"", path.String());
		result = RunSystemCommand(cmd.String());
	}
	else if (name == "read_file") {
		BString path = arguments.FindString("path");
		if (path.IsEmpty()) {
			return "{\"error\":\"Missing path argument\"}";
		}
		
		BFile file(path.String(), B_READ_ONLY);
		if (file.InitCheck() != B_OK) {
			result.SetToFormat("{\"error\":\"Cannot open file: %s\"}", strerror(file.InitCheck()));
		} else {
			off_t size = 0;
			file.GetSize(&size);
			if (size > 1024 * 1024) { // Limite 1MB per sicurezza
				return "{\"error\":\"File too large to read (max 1MB)\"}";
			}
			std::vector<char> buffer(size + 1);
			ssize_t bytesRead = file.Read(buffer.data(), size);
			if (bytesRead >= 0) {
				buffer[bytesRead] = '\0';
				result = buffer.data();
			} else {
				result.SetToFormat("{\"error\":\"Read error: %s\"}", strerror(bytesRead));
			}
		}
	}
	else if (name == "create_file") {
		BString path = arguments.FindString("path");
		BString content = arguments.FindString("content");
		if (path.IsEmpty()) {
			return "{\"error\":\"Missing path argument\"}";
		}
		
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
		BString path = arguments.FindString("path");
		if (path.IsEmpty()) {
			return "{\"error\":\"Missing path argument\"}";
		}
		
		status_t err = create_directory(path.String(), 0755);
		if (err == B_OK) {
			result = "Directory created successfully.";
		} else {
			result.SetToFormat("{\"error\":\"Failed to create directory: %s\"}", strerror(err));
		}
	}
	else if (name == "delete_file") {
		BString path = arguments.FindString("path");
		if (path.IsEmpty()) {
			return "{\"error\":\"Missing path argument\"}";
		}
		
		BEntry entry(path.String());
		if (!entry.Exists()) {
			return "{\"error\":\"File or directory does not exist\"}";
		}
		
		status_t err = entry.Remove();
		if (err == B_OK) {
			result = "Deleted successfully.";
		} else {
			BString cmd;
			cmd.SetToFormat("rm -rf \"%s\"", path.String());
			result = RunSystemCommand(cmd.String());
		}
	}
	else if (name == "search_text") {
		BString pattern = arguments.FindString("pattern");
		BString path = arguments.FindString("path");
		if (pattern.IsEmpty()) {
			return "{\"error\":\"Missing pattern argument\"}";
		}
		if (path.IsEmpty()) path = "/boot/home";
		
		pattern.ReplaceAll("\"", "\\\"");
		BString cmd;
		cmd.SetToFormat("grep -rn \"%s\" \"%s\"", pattern.String(), path.String());
		result = RunSystemCommand(cmd.String());
	}
	else if (name == "open_document") {
		BString path = arguments.FindString("path");
		if (path.IsEmpty()) {
			return "{\"error\":\"Missing path argument\"}";
		}
		
		BString cmd;
		cmd.SetToFormat("open \"%s\"", path.String());
		result = RunSystemCommand(cmd.String());
	}
	else if (name == "manage_attribute") {
		BString action = arguments.FindString("action"); // "read", "write", "list"
		BString path = arguments.FindString("path");
		BString attrName = arguments.FindString("name");
		BString attrValue = arguments.FindString("value");

		if (path.IsEmpty() || action.IsEmpty()) {
			return "{\"error\":\"Missing action or path arguments\"}";
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
			while (node.GetNextAttr(nameBuf) == B_OK) {
				attr_info info;
				if (node.GetAttrInfo(nameBuf, &info) == B_OK) {
					result << nameBuf << " (type: " << (int32)info.type << ", size: " << info.size << " bytes)\n";
				} else {
					result << nameBuf << "\n";
				}
			}
		}
		else if (action == "read") {
			if (attrName.IsEmpty()) {
				return "{\"error\":\"Missing attribute name for read action\"}";
			}
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
			if (attrName.IsEmpty() || attrValue.IsEmpty()) {
				return "{\"error\":\"Missing attribute name or value for write action\"}";
			}
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
	else if (name == "run_terminal_command") {
		BString cmd = arguments.FindString("cmd");
		if (cmd.IsEmpty()) {
			return "{\"error\":\"Missing cmd argument\"}";
		}
		result = RunSystemCommand(cmd.String());
	}
	else {
		result.SetToFormat("{\"error\":\"Unknown tool: %s\"}", tool_name);
	}

	return result;
}

static BMessage* CreateToolMessage(const char* name, const char* desc, BMessage* properties, const char* requiredField = nullptr) {
	BMessage* tool = new BMessage();
	tool->AddString("name", name);
	tool->AddString("description", desc);
	tool->AddInt32("exec_type", 1); // standard terminal or internal execution
	tool->AddString("exec_target", ""); // resolved internally

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
	// Svuota prima, deallocando correttamente gli oggetti BMessage allocati nell'heap
	for (int32 i = 0; i < mpcManager.CountItems(); i++) {
		delete (BMessage*)mpcManager.ItemAt(i);
	}
	mpcManager.MakeEmpty();

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

	// 2. Area File System (AI_PERM_FILE_SYSTEM)
	if (permissions & AI_PERM_FILE_SYSTEM) {
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
		{
			BMessage properties;
			BMessage pathProp;
			pathProp.AddString("type", "string");
			pathProp.AddString("description", "The absolute path of the file, image, application or document to open.");
			properties.AddMessage("path", &pathProp);
			mpcManager.AddItem(CreateToolMessage("open_document", 
				"Open any file, image, document, or application using the default Haiku preference application (like double-clicking).", &properties, "path"));
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
				"Read, write, or list BFS custom file attributes (like AI:plugin_type, BEOS:TYPE, or custom metadata) on a file in Haiku.", &properties, "action"));
		}
	}

	// 3. Area Comandi Terminale (AI_PERM_RUN_COMMANDS)
	if (permissions & AI_PERM_RUN_COMMANDS) {
		BMessage properties;
		BMessage cmdProp;
		cmdProp.AddString("type", "string");
		cmdProp.AddString("description", "The full terminal command string to execute.");
		properties.AddMessage("cmd", &cmdProp);
		mpcManager.AddItem(CreateToolMessage("run_terminal_command", 
			"Run any arbitrary terminal shell command under the current user context. WARNING: Use with extreme caution.", &properties, "cmd"));
	}
}
