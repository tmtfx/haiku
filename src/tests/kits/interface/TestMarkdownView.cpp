#include "MarkdownView.h"
#include <ScrollView.h>

// Dentro la tua finestra o AboutView:
BMarkdownView* mdView = new BMarkdownView("markdown_info");

// Opzionale: inseriscila in una BScrollView per consentire lo scorrimento
BScrollView* scrollView = new BScrollView("md_scroll", mdView, 0, false, true);

// Passa direttamente la tua stringa Markdown
mdView->SetMarkdown(
    "# Ahoy Pirate!\n\n"
    "Benvenuto in **Haiku OS Pirate Edition**.\n\n"
    "### Funzionalità principali:\n"
    "* Supporto *Markdown* nativo mediante `MD4C`.\n"
    "* Integrazione completa col **BeAPI** Layout System.\n"
    "* Codice sorgente e comandi tipo `pkgman install`.\n\n"
    "Usa il comando `help` nel terminale per saperne di più."
);

// Aggiungila al layout
layout->AddView(scrollView);