#include <Application.h>
#include <Window.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <Button.h>
#include <StringView.h>
#include <MarkdownView.h>
#include <Path.h>
#include <TranslationUtils.h>

// ID dei comandi per la BWindow
enum {
	MSG_SET_SAMPLE_MARKDOWN = 'mksp',
	MSG_CLEAR_MARKDOWN      = 'mkcl'
};

// --- Finestra di Test ---
class TestMarkdownWindow : public BWindow {
public:
	TestMarkdownWindow()
		: BWindow(BRect(100, 100, 800, 650), "MarkdownView Test - Full Features", 
				  B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE)
	{
		// 1. Istanziamo BMarkdownView
		fMarkdownView = new BMarkdownView("markdown_view");
		fMarkdownView->MakeEditable(false);

		// 2. Mettiamo la vista dentro BScrollView
		fScrollView = new BScrollView("markdown_scroll", fMarkdownView, 0, false, true);

		// 3. Pulsanti di controllo
		BButton* sampleBtn = new BButton("sample_btn", "Ricarica Sample", 
										 new BMessage(MSG_SET_SAMPLE_MARKDOWN));
		BButton* clearBtn  = new BButton("clear_btn", "Pulisci", 
										 new BMessage(MSG_CLEAR_MARKDOWN));

		// 4. Costruzione del Layout responsive usando BLayoutBuilder
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(new BStringView("title", "Test rendering nativo Markdown (Links, Tabelle & Immagini):"))
			.Add(fScrollView, 1.0)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.AddGlue()
				.Add(clearBtn)
				.Add(sampleBtn)
			.End();

		// Carichiamo il contenuto iniziale
		_LoadDefaultContent();
	}

	virtual void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case MSG_SET_SAMPLE_MARKDOWN:
				_LoadDefaultContent();
				break;

			case MSG_CLEAR_MARKDOWN:
				fMarkdownView->SetMarkdown("");
				break;

			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	void _LoadDefaultContent()
	{
		fMarkdownView->SetMarkdown(
			"# Ahoy Pirate! 🏴‍☠️\n\n"
			"Benvenuto nel test avanzato di **BMarkdownView** per Haiku OS.\n\n"
			"### 🔗 Test Collegamenti Ipertestuali (BUrl / B_LINK_TEXT_COLOR):\n"
			"* Visita il sito ufficiale: [Haiku OS Website](https://www.haiku-os.org)\n"
			"* Apri il tracker dei bug: [Haiku Bugtracker](https://dev.haiku-os.org)\n"
			"* Invia un'email di test: [Dev Mail](mailto:developer@example.com)\n\n"
			"---\n\n"
			"### 📊 Test Tabelle (Zebra Striping & Fixed Font):\n\n"
			"| Componente | Tipo | Stato |\n"
			"| :--- | :---: | ---: |\n"
			"| `MD4C Parser` | C Library | ✅ Integrato |\n"
			"| `BTextView` | Native View | ✅ Esteso |\n"
			"| `BUrl` | Network Kit | ✅ Attivo |\n"
			"| `Translation Kit` | Graphics | ✅ Operativo |\n\n"
			"---\n\n"
			"### 🖼️ Test Immagini (Translation Kit & BBitmap):\n"
			"Di seguito un'icona di sistema di Haiku caricata via percorso locale:\n\n"
			"![Haiku Logo](/boot/home/Progjets/haiku/data/artwork/logo.png)\n\n"
			"---\n\n"
			"### 💻 Codice e Quotes:\n"
			"```cpp\n"
			"// Test apertura URL con il colore nativo di sistema\n"
			"BUrl url(\"[https://www.haiku-os.org](https://www.haiku-os.org)\");\n"
			"if (url.IsValid())\n"
			"    url.OpenWithPreferredApp();\n"
			"```\n\n"
			"> \"L'eleganza di BeOS incontra la potenza del parsing moderno.\"\n"
		);
	}

	BMarkdownView* fMarkdownView;
	BScrollView*   fScrollView;
};

// --- Applicazione ---
class TestMarkdownApp : public BApplication {
public:
	TestMarkdownApp()
		: BApplication("application/x-vnd.Haiku-TestMarkdownView")
	{
	}

	virtual void ReadyToRun() override
	{
		TestMarkdownWindow* window = new TestMarkdownWindow();
		window->Show();
	}
};

int main()
{
	TestMarkdownApp app;
	app.Run();
	return 0;
}
