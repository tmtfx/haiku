#include "MarkdownView.h"

#include <InterfaceDefs.h>
#include <algorithm>

BMarkdownView::BMarkdownView(const char* name, uint32 flags)
	:
	BTextView(name, flags),
	fCodeBlocks(20) // 20 elementi iniziali, true = la lista elimina i puntatori con delete
{
	fRawMarkdown.SetTo("");
	MakeEditable(false);
	MakeSelectable(true);
	SetStylable(true);
}

BMarkdownView::BMarkdownView(const char* name, const BFont* font,
	const rgb_color* color, uint32 flags)
	:
	BTextView(name, font, color, flags),
	fCodeBlocks(20)
{
	fRawMarkdown.SetTo("");
	MakeEditable(false);
	MakeSelectable(true);
	SetStylable(true);
}

BMarkdownView::~BMarkdownView()
{
}
/*
void
BMarkdownView::Draw(BRect updateRect)
{
	// 1. Prepariamo lo stato della vista per il disegno dello sfondo custom
	PushState();

	rgb_color docBg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	// Calcoliamo la luminanza per capire se il tema corrente è chiaro o scuro
	// Luma formula: 0.299 R + 0.587 G + 0.114 B
	float luminance = (0.299f * docBg.red + 0.587f * docBg.green + 0.114f * docBg.blue);
	float tint = (luminance < 128.0f) ? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT;

	rgb_color blockBgColor = tint_color(docBg, tint);
	SetHighColor(blockBgColor);

	// 2. Disegniamo il riquadro arrotondato per ogni blocco di codice
	int32 count = fCodeBlocks.CountItems();
	for (int32 i = 0; i < count; i++) {
		CodeBlockRegion* block = fCodeBlocks.ItemAt(i);
		if (block == NULL || block->startPos >= block->endPos)
			continue;

		// Recuperiamo le coordinate geometriche della regione di testo
		BPoint startPt = PointAt(block->startPos);
		BPoint endPt = PointAt(block->endPos);

		// Costruiamo il rettangolo occupato dal blocco di codice
		BRect blockRect;
		blockRect.left = 0;
		blockRect.right = Bounds().Width();
		blockRect.top = startPt.y;
		blockRect.bottom = endPt.y + LineHeight(block->endPos);

		// Aggiungiamo un piccolo margine (padding) estetico
		blockRect.InsetBy(2.0f, 1.0f);

		if (blockRect.Intersects(updateRect)) {
			FillRoundRect(blockRect, 6.0f, 6.0f);
		}
	}

	PopState();

	// 3. Disegniamo il testo e la selezione nativa della BTextView
	BTextView::Draw(updateRect);
}*/
void
BMarkdownView::Draw(BRect updateRect)
{
	PushState();

	rgb_color docBg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);

	float luminance = (0.299f * docBg.red + 0.587f * docBg.green + 0.114f * docBg.blue);
	float tint = (luminance < 128.0f) ? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT;

	rgb_color blockBgColor = tint_color(docBg, tint);
	SetHighColor(blockBgColor);

	int32 count = fCodeBlocks.CountItems();
	for (int32 i = 0; i < count; i++) {
		CodeBlockRegion* block = fCodeBlocks.ItemAt(i);
		if (block == NULL || block->startPos >= block->endPos)
			continue;

		BPoint startPt = PointAt(block->startPos);
		BPoint endPt = PointAt(block->endPos);

		BRect blockRect;
		// Estendiamo il rettangolo per coprire tutto il margine sinistro/destro visibile
		blockRect.left = 0.0f;
		blockRect.right = Bounds().Width();
		
		// Allineiamo il top e il bottom alle righe di testo effettive
		blockRect.top = startPt.y - 1.0f;
		blockRect.bottom = endPt.y + LineHeight(block->endPos) + 1.0f;

		// Riduciamo leggermente i bordi laterali per fare respirare la UI
		blockRect.InsetBy(2.0f, 0.0f);

		if (blockRect.Intersects(updateRect)) {
			FillRoundRect(blockRect, 4.0f, 4.0f);
		}
	}

	PopState();

	BTextView::Draw(updateRect);
}

status_t
BMarkdownView::SetMarkdown(const BString& markdownText)
{
	return SetMarkdown(markdownText.String());
}

status_t
BMarkdownView::SetMarkdown(const char* markdownText)
{
	SetText("");
	fCodeBlocks.MakeEmpty(true); // Svuota la lista ed elimina gli oggetti allocati
	fRawMarkdown.SetTo(markdownText);

	if (markdownText == NULL || strlen(markdownText) == 0)
		return B_OK;

	RenderState state;
	state.view = this;
	
	SetFontAndColor(be_plain_font);
	GetFont(&state.baseFont);
	state.currentFont = state.baseFont;
	
	state.textColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	state.codeColor = (rgb_color){ 200, 40, 40, 255 };

	MD_PARSER parser = {
		0,
		MD_FLAG_TABLES,
		_EnterBlockCb,
		_LeaveBlockCb,
		_EnterSpanCb,
		_LeaveSpanCb,
		_TextCb,
		NULL,
		NULL
	};

	int result = md_parse(markdownText, (MD_SIZE)strlen(markdownText), &parser, &state);
	Invalidate(); // Richiede il ridisegno per applicare i riquadri di sfondo
	return (result == 0) ? B_OK : B_ERROR;
}

void BMarkdownView::InsertRaw(int32 offset, const char* text, int32 length)
{
	fRawMarkdown.Insert(text, length, offset);
	SetMarkdown(fRawMarkdown);
}

void BMarkdownView::InsertRaw(const char* text, int32 length)
{
	InsertRaw(fRawMarkdown.Length(), text, length);
}

void BMarkdownView::InsertRaw(const char* text)
{
	InsertRaw(fRawMarkdown.Length(), text, strlen(text));
}

int32 BMarkdownView::RawTextLength() const
{
	return fRawMarkdown.Length();
}

const char*
BMarkdownView::RawText() const
{
	return fRawMarkdown.String();
}

void
BMarkdownView::_ApplyCurrentStyle(int32 startPos, RenderState& state)
{
	int32 endPos = TextLength();
	if (startPos >= endPos)
		return;

	uint16 face = B_REGULAR_FACE;
	if (state.isBold || state.headingLevel > 0)
		face |= B_BOLD_FACE;
	if (state.isItalic)
		face |= B_ITALIC_FACE;

	if (state.isCode || state.isBlockCode)
		state.currentFont = *be_fixed_font;
	else
		state.currentFont = state.baseFont;

	state.currentFont.SetFace(face);

	if (state.headingLevel > 0) {
		float factor = 1.0f + (0.15f * (7 - std::min(state.headingLevel, (uint32)6)));
		state.currentFont.SetSize(state.baseFont.Size() * factor);
	} else {
		state.currentFont.SetSize(state.baseFont.Size());
	}

	rgb_color colorToApply = (state.isCode || state.isBlockCode) 
		? state.codeColor 
		: state.textColor;

	SetFontAndColor(startPos, endPos, &state.currentFont, B_FONT_ALL, &colorToApply);
}

// -----------------------------------------------------------------------------
// Callbacks MD4C
// -----------------------------------------------------------------------------

int
BMarkdownView::_EnterBlockCb(MD_BLOCKTYPE type, void* detail, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);

	switch (type) {
		case MD_BLOCK_H: {
			MD_BLOCK_H_DETAIL* hDetail = static_cast<MD_BLOCK_H_DETAIL*>(detail);
			state->headingLevel = hDetail->level;
			break;
		}
		case MD_BLOCK_CODE:
			state->isBlockCode = true;
			state->view->Insert("\n");
			state->currentBlockStart = state->view->TextLength();
			break;
		default:
			break;
	}
	return 0;
}

int
BMarkdownView::_LeaveBlockCb(MD_BLOCKTYPE type, void* detail, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);

	switch (type) {
		case MD_BLOCK_H:
			state->headingLevel = 0;
			state->view->Insert("\n\n");
			break;
		case MD_BLOCK_P:
			state->view->Insert("\n\n");
			break;
		case MD_BLOCK_CODE: {
			state->isBlockCode = false;
			int32 blockEnd = state->view->TextLength();
			
			if (state->currentBlockStart != -1 && blockEnd > state->currentBlockStart) {
				CodeBlockRegion* region = new CodeBlockRegion();
				region->startPos = state->currentBlockStart;
				region->endPos = blockEnd;
				state->view->fCodeBlocks.AddItem(region);
			}
			state->currentBlockStart = -1;
			state->view->Insert("\n\n");
			break;
		}
		case MD_BLOCK_LI:
			state->view->Insert("\n");
			break;
		default:
			break;
	}
	return 0;
}

int
BMarkdownView::_EnterSpanCb(MD_SPANTYPE type, void* detail, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);

	switch (type) {
		case MD_SPAN_STRONG:
			state->isBold = true;
			break;
		case MD_SPAN_EM:
			state->isItalic = true;
			break;
		case MD_SPAN_CODE:
			state->isCode = true;
			break;
		default:
			break;
	}
	return 0;
}

int
BMarkdownView::_LeaveSpanCb(MD_SPANTYPE type, void* detail, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);

	switch (type) {
		case MD_SPAN_STRONG:
			state->isBold = false;
			break;
		case MD_SPAN_EM:
			state->isItalic = false;
			break;
		case MD_SPAN_CODE:
			state->isCode = false;
			break;
		default:
			break;
	}
	return 0;
}

int
BMarkdownView::_TextCb(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);
	
	int32 startPos = state->view->TextLength();
	
	BString str(text, size);
	state->view->Insert(str.String());

	state->view->_ApplyCurrentStyle(startPos, *state);

	return 0;
}
