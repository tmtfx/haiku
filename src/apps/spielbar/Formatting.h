/*
 * SpielBar v3 - stringhe per l'interfaccia: conteggi, descrizioni, tooltip
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_FORMATTING_H
#define SPIELBAR_FORMATTING_H

#include <String.h>
#include <SupportDefs.h>

#include "SpielBarTypes.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// Tooltip: la scorciatoia sta fuori dalla stringa tradotta.

BString
ToolTipWith(const char* label, const char* shortcut);

BString
DescribeView(const ViewSpec& s);

BString
FormatCount(int32 n, int mode);	// 0 elementi, 1 risultati, 2 risultati troncati

#endif	// SPIELBAR_FORMATTING_H
