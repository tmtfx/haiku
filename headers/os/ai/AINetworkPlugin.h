/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef AI_NETWORK_PLUGIN_H
#define AI_NETWORK_PLUGIN_H

#include <UrlProtocolListener.h>

class SyncListener : public BUrlProtocolListener {
public:
    SyncListener() {}
    virtual ~SyncListener() {}
    
    bool CertificateVerificationFailed(BUrlRequest* request, 
                                       BCertificate& certificate, 
                                       const char* message) override {
        return false; 
    }
};


class CompletionListener : public SyncListener {
public:
    CompletionListener(const char* notifyPath) : fPath(notifyPath) {}

    virtual void RequestCompleted(BUrlRequest* caller, bool success) override {
        BFile file(fPath.String(), B_WRITE_ONLY | B_OPEN_AT_END);
        if (file.InitCheck() == B_OK) {
            BString endMarker = "<<STREAM_END>>";
            file.Write(endMarker.String(), endMarker.Length());
        }
    }
private:
    BString fPath;
};

#endif // AI_NETWORK_PLUGIN_H
