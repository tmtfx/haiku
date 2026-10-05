#ifndef MARKDOWN_VIEW_H
#define MARKDOWN_VIEW_H

#include <TextView.h>
#include <String.h>
#include <Font.h>
#include <Bitmap.h>
#include <GraphicsDefs.h>
#include <ObjectList.h>
#include <Cursor.h>
#include <md4c.h>

// Struttura per tracciare le regioni dei blocchi di codice nel testo
struct QuoteRegion {
	int32 startPos;
	int32 endPos;
};

struct HorizontalRuleRegion {
    int32 pos;
};

struct LinkRegion {
	int32	startPos;
	int32	endPos;
	BString	url;

	LinkRegion() : startPos(-1), endPos(-1) {}
};

struct TableCellRegion {
	int32 startPos;
	int32 endPos;
	int32 colIndex;
	BString text;
};
struct TableRowRegion {
	int32 startPos;
	int32 endPos;
	bool isHeader;
	BObjectList<TableCellRegion, true> cells; // <- Salviamo le celle della riga

	TableRowRegion()
		: startPos(-1),
		  endPos(-1),
		  isHeader(false),
		  cells(10)
	{}
};

struct TableRegion {
	int32 startPos;
	int32 endPos;
	BObjectList<TableRowRegion> rows;

	TableRegion() : rows(10) {}
};

struct ImageRegion {
	int32		startPos;
	int32		endPos;
	BString		src;
	BString		alt;
	BBitmap*	bitmap;

	ImageRegion()
		: startPos(-1), endPos(-1), bitmap(NULL) {}

	~ImageRegion() {
		delete bitmap;
	}
};

struct CodeBlockRegion {
	int32	startPos;
	int32	endPos;
};

class BMarkdownView : public BTextView {
public:
							BMarkdownView(const char* name,
								uint32 flags = B_WILL_DRAW | B_NAVIGABLE);
							BMarkdownView(const char* name,
								const BFont* font, const rgb_color* color,
								uint32 flags = B_WILL_DRAW | B_NAVIGABLE);
							BMarkdownView(BMessage* archive);
	virtual					~BMarkdownView();

	static	BArchivable*	Instantiate(BMessage* archive);
	virtual	status_t		Archive(BMessage* archive, bool deep = true) const override;
	
	// Override di BView per il rendering dello sfondo custom dei blocchi
	virtual void			Draw(BRect updateRect) override;
	
	//virtual void AttachedToWindow();
	virtual void FrameResized(float width, float height);
	
	virtual void			MouseDown(BPoint where) override;
	virtual void			MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage) override;

	void					InsertRaw(const char* text);
	void					InsertRaw(const char* text, int32 length);
	void					InsertRaw(int32 offset, const char* text,
									int32 length);
	int32					RawTextLength() const;
	
	const char*				RawText() const;
	// Imposta il testo Markdown ed esegue il parsing
	status_t				SetMarkdown(const char* markdownText);
	status_t				SetMarkdown(const BString& markdownText);

	// Metodi virtuali di BView per il BeAPI Layout System
	// virtual BSize			MinSize() override;
	// virtual BSize			PreferredSize() override;
	// virtual BSize			MaxSize() override;

private:
	// Disabilitiamo copia e assegnazione per sicurezza BeAPI
	BMarkdownView(const BMarkdownView&);
	BMarkdownView& operator=(const BMarkdownView&);
	// Struttura di stato interna usata dal parser durante il traversal di MD4C
	struct RenderState {
		BMarkdownView*		view;
		BFont				currentFont;
		rgb_color			textColor;
		rgb_color			codeColor;
		
		bool				isBold;
		bool				isItalic;
		bool				isCode;
		bool				isBlockCode;
		uint32				headingLevel;
		int32				currentBlockStart;
		bool				isTable;
		bool				isHeaderRow;
		TableRegion*		currentTable;
		int32				currentTRStart;
		int32				currentColIndex;
		TableCellRegion*	currentCell;
		bool				isImage;
		ImageRegion*		currentImage;
		BString				currentImageAlt;
		bool				isLink;
		LinkRegion*			currentLink;
		int32				listDepth;
		bool				isOrderedList;
		int32				olItemNumber;
		bool				isQuote;
		QuoteRegion*		currentQuote;
		
		
		RenderState()
		: view(NULL),
		  textColor(make_color(0, 0, 0)),
		  codeColor(make_color(220, 50, 50)),
		  isBold(false),
		  isItalic(false),
		  isCode(false),
		  isBlockCode(false),
		  headingLevel(0),
		  currentBlockStart(-1),
		  isTable(false),
		  isHeaderRow(false),
		  currentTable(NULL),
		  currentTRStart(-1),
		  isImage(false),
		  currentImage(NULL),
		  isLink(false),
		  currentLink(NULL),
		  listDepth(0),
		  isOrderedList(false),
		  olItemNumber(1),
		  isQuote(false),
		  currentQuote(NULL)
	{}
	};

	void                    _Init();
	void                    _ClearRegions();
	void					_ApplyCurrentStyle(int32 startPos, RenderState& state);

	// Callbacks C richieste da MD4C
	static int				_EnterBlockCb(MD_BLOCKTYPE type, void* detail, void* userdata);
	static int				_LeaveBlockCb(MD_BLOCKTYPE type, void* detail, void* userdata);
	static int				_EnterSpanCb(MD_SPANTYPE type, void* detail, void* userdata);
	static int				_LeaveSpanCb(MD_SPANTYPE type, void* detail, void* userdata);
	static int				_TextCb(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata);

	void					_LoadImageForRegion(ImageRegion* region);

	BString					fRawMarkdown;
	BCursor					fHandCursor;
	BObjectList<CodeBlockRegion, true> fCodeBlocks;
	BObjectList<TableRegion> fTables;
	BObjectList<ImageRegion> fImages;
	BObjectList<LinkRegion> fLinks;
	LinkRegion*             _LinkAt(BPoint point) const;
	BList					fHorizontalRules;
	BObjectList<QuoteRegion, true> fQuotes;

};

#endif // MARKDOWN_VIEW_H
