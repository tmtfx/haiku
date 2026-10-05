#include "MarkdownView.h"

#include <InterfaceDefs.h>
#include <TranslationUtils.h>// <- circular dependency replaced by
// These to not use translationutils
//#include <BitmapStream.h>
//#include <File.h>
//#include <TranslatorRoster.h>
// --------------------------
#include <algorithm>
#include <Cursor.h>
#include <Path.h>
#include <Window.h>
#include <Url.h>

#include <cstdio>

BMarkdownView::BMarkdownView(const char* name, uint32 flags)
    :
    BTextView(name, flags),
    fHandCursor(B_CURSOR_ID_FOLLOW_LINK),
    fCodeBlocks(20),
    fTables(10),
    fImages(10),
    fLinks(20),
    fQuotes(20)
{
    _Init();
}


BMarkdownView::BMarkdownView(const char* name, const BFont* font,
    const rgb_color* color, uint32 flags)
    :
    BTextView(name, font, color, flags),
    fHandCursor(B_CURSOR_ID_FOLLOW_LINK),
    fCodeBlocks(20),
    fTables(10),
    fImages(10),
    fLinks(20),
    fQuotes(20)
{
    _Init();
}
BMarkdownView::BMarkdownView(BMessage* archive)
	:
	BTextView(archive),
	fHandCursor(B_CURSOR_ID_FOLLOW_LINK),
	fCodeBlocks(20),
	fTables(10),
	fImages(10),
	fLinks(20),
    fQuotes(20)
{
	_Init();
}


BArchivable*
BMarkdownView::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BMarkdownView"))
		return new BMarkdownView(archive);

	return NULL;
}


status_t
BMarkdownView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BTextView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("class", "BMarkdownView");

	return status;
}

void
BMarkdownView::_Init()
{
    fRawMarkdown.SetTo("");
    MakeEditable(false);
    MakeSelectable(true);
    SetStylable(true);
}
void
BMarkdownView::_ClearRegions()
{
    // BObjectList<..., true> elimina automaticamente gli oggetti con MakeEmpty()
    fCodeBlocks.MakeEmpty();
    fTables.MakeEmpty();
    fImages.MakeEmpty();
    fLinks.MakeEmpty();

    // Per BList dobbiamo eliminare manualmente gli elementi
    for (int32 i = 0; i < fHorizontalRules.CountItems(); i++) {
        delete static_cast<HorizontalRuleRegion*>(fHorizontalRules.ItemAt(i));
    }
    fHorizontalRules.MakeEmpty();
    
    fQuotes.MakeEmpty();
}


BMarkdownView::~BMarkdownView()
{
    _ClearRegions();
}
void
BMarkdownView::Draw(BRect updateRect)
{
	// 1. BTextView disegna tutto il testo (compreso il testo chiaro del codice)
	// ma lo fa sullo sfondo bianco standard del documento.
	BTextView::Draw(updateRect);

	PushState();
	// rendering citazioni
	int32 quoteCount = fQuotes.CountItems();
	if (quoteCount > 0) {
		PushState();

		rgb_color panelColor = ui_color(B_PANEL_BACKGROUND_COLOR);
		rgb_color bgColor = tint_color(panelColor, B_DARKEN_1_TINT);
		rgb_color barColor = tint_color(panelColor, B_DARKEN_3_TINT);

		for (int32 i = 0; i < quoteCount; i++) {
			QuoteRegion* quote = fQuotes.ItemAt(i);
			if (quote == NULL || quote->startPos < 0 || quote->startPos >= quote->endPos)
				continue;

			BPoint startPt = PointAt(quote->startPos);
			int32 endPosAdjusted = std::max(quote->startPos, quote->endPos - 1);
			BPoint endPt = PointAt(endPosAdjusted);

			//float fontHeight = LineHeight(quote->startPos);

			BRect quoteRect;
			quoteRect.left = 2.0f;
			quoteRect.right = Bounds().Width() - 2.0f;
			quoteRect.top = startPt.y - 1.0f;
			quoteRect.bottom = endPt.y + LineHeight(endPosAdjusted) + 1.0f;

			if (quoteRect.Intersects(updateRect)) {
				// 1. Sfondo pieno della citazione
				SetDrawingMode(B_OP_COPY);
				SetHighColor(bgColor);
				FillRect(quoteRect);

				// 2. Barra d'accento verticale a sinistra (spessa 4px)
				BRect barRect(quoteRect.left, quoteRect.top, quoteRect.left + 4.0f, quoteRect.bottom);
				SetHighColor(barColor);
				FillRect(barRect);

				// 3. Ridisegno del testo della citazione sopra lo sfondo
				SetDrawingMode(B_OP_OVER);

				int32 currentOffset = quote->startPos;
				while (currentOffset < quote->endPos) {
					BPoint linePt = PointAt(currentOffset);

					int32 lineEnd = currentOffset;
					while (lineEnd < quote->endPos && ByteAt(lineEnd) != '\n') {
						lineEnd++;
					}

					int32 length = lineEnd - currentOffset;
					if (length > 0) {
						BString lineStr;
						GetText(currentOffset, length, lineStr.LockBuffer(length + 1));
						lineStr.UnlockBuffer();

						// Recuperiamo e applichiamo lo stile del font presente in quel punto
						BFont lineFont;
						rgb_color lineTextColor;
						GetFontAndColor(currentOffset, &lineFont, &lineTextColor);

						SetFont(&lineFont);
						SetHighColor(lineTextColor);

						font_height fh;
						lineFont.GetHeight(&fh);

						// Tracciamo la riga di testo posizionata sulla linea di base visiva
						DrawString(lineStr.String(), BPoint(linePt.x, linePt.y + fh.ascent));
					}

					currentOffset = lineEnd + 1;
				}
			}
		}
	}
	// rendering divisori
	int32 hrCount = fHorizontalRules.CountItems();
    if (hrCount > 0) {
    	// Colore della linea: un grigio discreto di sistema
        rgb_color dividerColor = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
        SetHighColor(dividerColor);
        SetPenSize(1.0f);

        float viewWidth = Bounds().Width();
        float leftMargin = 10.0f;
        float rightMargin = viewWidth - 10.0f;

        font_height fh;
        be_plain_font->GetHeight(&fh);
        float lineHeight = fh.ascent + fh.descent + fh.leading;

        for (int32 i = 0; i < hrCount; i++) {
            HorizontalRuleRegion* hr = static_cast<HorizontalRuleRegion*>(fHorizontalRules.ItemAt(i));
            if (hr == NULL || hr->pos < 0 || hr->pos > TextLength())
                continue;

            // Coordinate visive dell'offset di ancoraggio
            BPoint pt = PointAt(hr->pos);

            // Calcoliamo la Y centrata nel gap del newline
            float y = pt.y + (lineHeight / 2.0f);

            BRect hrRect(leftMargin, y - 1.0f, rightMargin, y + 1.0f);
            if (hrRect.Intersects(updateRect)) {
                StrokeLine(BPoint(leftMargin, y), BPoint(rightMargin, y));
            }
        }
    }
	
	// rendering di immagini se esistono
	int32 imageCount = fImages.CountItems();
	if (imageCount > 0) {
		for (int32 i = 0; i < imageCount; i++) {
			ImageRegion* img = fImages.ItemAt(i);
			if (img == NULL || img->bitmap == NULL)
				continue;

			BPoint startPt = PointAt(img->startPos);
			BRect bitmapBounds = img->bitmap->Bounds();

			// Scaliamo l'immagine se supera la larghezza massima della vista
			float maxWidth = Bounds().Width() - 10.0f;
			float imgWidth = bitmapBounds.Width();
			float imgHeight = bitmapBounds.Height();

			if (imgWidth > maxWidth && maxWidth > 0.0f) {
				float scale = maxWidth / imgWidth;
				imgWidth = maxWidth;
				imgHeight *= scale;
			}

			BRect drawRect(
				startPt.x + 5.0f,
				startPt.y + 2.0f,
				startPt.x + 5.0f + imgWidth,
				startPt.y + 2.0f + imgHeight
			);

			if (drawRect.Intersects(updateRect)) {
				SetDrawingMode(B_OP_ALPHA);
				SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
				DrawBitmap(img->bitmap, img->bitmap->Bounds(), drawRect);
			}
		}
	}

	rgb_color docBg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	float luminance = (0.299f * docBg.red + 0.587f * docBg.green + 0.114f * docBg.blue);
	
	// if there's no tables just skip this part
	int32 tableCount = fTables.CountItems();
	if ( tableCount > 0) {
		bool isDark = (luminance < 128.0f);
		// Palette colori in stile GitHub (Chiaro / Scuro)
		rgb_color tableBorderColor  = isDark ? (rgb_color){ 60, 65, 70, 255 } : (rgb_color){ 210, 215, 220, 255 };
		rgb_color headerBgColor      = isDark ? (rgb_color){ 45, 50, 55, 255 } : (rgb_color){ 240, 243, 246, 255 };
		rgb_color altRowBgColor     = isDark ? (rgb_color){ 35, 38, 42, 255 } : (rgb_color){ 248, 249, 250, 255 };
		rgb_color normalRowBgColor  = isDark ? (rgb_color){ 28, 30, 33, 255 } : (rgb_color){ 255, 255, 255, 255 };

		// -------------------------------------------------------------------------
		// A. RENDERING DELLE TABELLE (Sfondi alternati e Bordi)
		// -------------------------------------------------------------------------
		
		for (int32 t = 0; t < tableCount; t++) {
			TableRegion* table = fTables.ItemAt(t);
			if (table == NULL || table->rows.CountItems() == 0)
				continue;

			int32 rowCount = table->rows.CountItems();
			
			// Calcoliamo i limiti dell'intera tabella
			TableRowRegion* firstRow = table->rows.ItemAt(0);
			TableRowRegion* lastRow  = table->rows.ItemAt(rowCount - 1);

			BPoint startPt = PointAt(firstRow->startPos);
			int32 lastPosAdjusted = std::max(lastRow->startPos, lastRow->endPos - 1);
			BPoint endPt = PointAt(lastPosAdjusted);

			BRect totalTableRect;
			totalTableRect.left = 0.0f;
			totalTableRect.right = Bounds().Width();
			totalTableRect.top = startPt.y - 1.0f;
			totalTableRect.bottom = endPt.y + LineHeight(lastPosAdjusted) + 1.0f;
			totalTableRect.InsetBy(2.0f, 0.0f);

			if (!totalTableRect.Intersects(updateRect))
				continue;

			// 1. Sfondo delle righe (Zebra striping + Intestazione)
			for (int32 r = 0; r < rowCount; r++) {
				TableRowRegion* row = table->rows.ItemAt(r);
				BPoint rStartPt = PointAt(row->startPos);
				int32 rEndAdjusted = std::max(row->startPos, row->endPos - 1);
				
				BRect rowRect;
				rowRect.left = totalTableRect.left;
				rowRect.right = totalTableRect.right;
				rowRect.top = rStartPt.y - 1.0f;
				rowRect.bottom = rStartPt.y + LineHeight(rEndAdjusted) + 1.0f;

				rgb_color rowBg;
				if (row->isHeader)
					rowBg = headerBgColor;
				else if (r % 2 == 1)
					rowBg = altRowBgColor;
				else
					rowBg = normalRowBgColor;

				SetDrawingMode(B_OP_COPY);
				SetHighColor(rowBg);
				FillRect(rowRect);

				// Linea divisoria orizzontale sotto ogni riga
				SetHighColor(tableBorderColor);
				StrokeLine(BPoint(rowRect.left, rowRect.bottom), BPoint(rowRect.right, rowRect.bottom));
			}

			// 2. Bordo esterno arrotondato dell'intera tabella
			SetHighColor(tableBorderColor);
			StrokeRoundRect(totalTableRect, 4.0f, 4.0f);

			// 3. Ridisegniamo il testo della tabella sopra lo sfondo disegnato
			SetDrawingMode(B_OP_OVER);
			SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
			SetFont(be_fixed_font);

			for (int32 r = 0; r < rowCount; r++) {
				TableRowRegion* row = table->rows.ItemAt(r);
				int32 currentOffset = row->startPos;
				
				while (currentOffset < row->endPos) {
					BPoint linePt = PointAt(currentOffset);
					int32 lineEnd = currentOffset;
					while (lineEnd < row->endPos && ByteAt(lineEnd) != '\n') {
						lineEnd++;
					}

					int32 length = lineEnd - currentOffset;
					if (length > 0) {
						BString lineStr;
						GetText(currentOffset, length, lineStr.LockBuffer(length + 1));
						lineStr.UnlockBuffer();

						DrawString(lineStr.String(), BPoint(linePt.x, linePt.y + LineHeight(currentOffset) - 3.0f));
					}
					currentOffset = lineEnd + 1;
				}
			}
		}
	}
	
	// if there's no codeblocks just exit!
	int32 count = fCodeBlocks.CountItems();
	if (count > 0) {
		// Sfondo del riquadro invertito
		rgb_color blockBgColor;
		rgb_color codeTextColor;
		
		if (luminance >= 128.0f) {
			// Tema Chiaro -> Riquadro Scuro, Testo Chiaro
			blockBgColor  = (rgb_color){ 35, 38, 41, 255 };
			codeTextColor = (rgb_color){ 235, 238, 242, 255 };
		} else {
			// Tema Scuro -> Riquadro Chiaro/Giallino, Testo Scuro
			blockBgColor  = (rgb_color){ 245, 242, 220, 255 };
			codeTextColor = (rgb_color){ 25, 25, 25, 255 };
		}	
		
		for (int32 i = 0; i < count; i++) {
			CodeBlockRegion* block = fCodeBlocks.ItemAt(i);
			if (block == NULL || block->startPos >= block->endPos)
				continue;

			BPoint startPt = PointAt(block->startPos);
			int32 endPosAdjusted = std::max(block->startPos, block->endPos - 1);
			BPoint endPt = PointAt(endPosAdjusted);

			BRect blockRect;
			blockRect.left = 0.0f;
			blockRect.right = Bounds().Width();
			blockRect.top = startPt.y - 1.0f;
			blockRect.bottom = endPt.y + LineHeight(endPosAdjusted) + 1.0f;

			blockRect.InsetBy(2.0f, 0.0f);

			if (blockRect.Intersects(updateRect)) {
				// A. Disegniamo lo sfondo pieno del riquadro (coprendo l'area del codice)
				SetDrawingMode(B_OP_COPY);
				SetHighColor(blockBgColor);
				FillRoundRect(blockRect, 4.0f, 4.0f);

				// B. Ridisegniamo il testo del codice sopra al riquadro con il colore dedicato
				SetDrawingMode(B_OP_OVER);
				SetHighColor(codeTextColor);
				SetFont(be_fixed_font);

				int32 currentOffset = block->startPos;
				while (currentOffset < block->endPos) {
					BPoint linePt = PointAt(currentOffset);
					
					int32 lineEnd = currentOffset;
					while (lineEnd < block->endPos && ByteAt(lineEnd) != '\n') {
						lineEnd++;
					}

					int32 length = lineEnd - currentOffset;
					if (length > 0) {
						BString lineStr;
						GetText(currentOffset, length, lineStr.LockBuffer(length + 1));
						lineStr.UnlockBuffer();

						DrawString(lineStr.String(), BPoint(linePt.x, linePt.y + LineHeight(currentOffset) - 3.0f));
					}

					currentOffset = lineEnd + 1;
				}
			}
		}
	}
	PopState();
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
	_ClearRegions();
	
	if (markdownText == NULL || strlen(markdownText) == 0)
		return B_BAD_VALUE;

	fRawMarkdown.SetTo(markdownText);

	RenderState state;
	state.view = this;
	
	SetFontAndColor(be_plain_font);
	GetFont(&state.baseFont);
	state.currentFont = state.baseFont;
	
	state.textColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	state.codeColor = (rgb_color){ 200, 40, 40, 255 }; // Usato solo per il codice inline `testo`

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
	Invalidate();
	return (result == 0) ? B_OK : B_ERROR;
}
/*
void
BMarkdownView::AttachedToWindow()
{
	BTextView::AttachedToWindow();

	// Ora che la vista è agganciata alla finestra, Bounds().Width() è valido!
	// Se ci sono immagini, rieseguiamo il parsing per calcolare le righe esatte
	if (fImages.CountItems() > 0 && !fRawMarkdown.IsEmpty()) {
		SetMarkdown(fRawMarkdown);
	}
}*/

void
BMarkdownView::FrameResized(float width, float height)
{
	BTextView::FrameResized(width, height);

	if (fImages.CountItems() > 0 && !fRawMarkdown.IsEmpty()) {
		SetMarkdown(fRawMarkdown);
	}
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
	
	if (state.isCode || state.isBlockCode || state.isTable) {
		state.currentFont = *be_fixed_font;
	} else if (state.isBold || state.headingLevel > 0) {
		state.currentFont = *be_bold_font;
	} else {
		state.currentFont = *be_plain_font;
	}

	uint16 face = state.currentFont.Face();
	if (state.isItalic)
		face |= B_ITALIC_FACE;
	if (state.isLink)
		face |= B_UNDERSCORE_FACE; // Sottolineato per i collegamenti

	state.currentFont.SetFace(face);

	if (state.headingLevel > 0) {
		float factor = 1.0f + (0.15f * (7 - std::min(state.headingLevel, (uint32)6)));
		state.currentFont.SetSize(state.baseFont.Size() * factor);
	} else {
		state.currentFont.SetSize(state.baseFont.Size());
	}

	// Selezione del colore del font
	rgb_color colorToApply;
	if (state.isLink) {
		// Blu classico o colore di sistema per i link
		colorToApply = ui_color(B_LINK_TEXT_COLOR);
	} else if (state.isBlockCode) {
		// Nei blocchi usiamo il colore ad alto contrasto per il riquadro invertito
		colorToApply = state.codeColor;
	} else if (state.isCode) {
		// Nel codice inline (`testo`) usiamo una tinta di evidenziazione
		colorToApply = (rgb_color){ 200, 40, 40, 255 };
	} else {
		colorToApply = state.textColor;
	}
	
	SetFontAndColor(startPos, endPos, &state.currentFont, B_FONT_ALL, &colorToApply); //
}

// -----------------------------------------------------------------------------
// Callbacks MD4C
// -----------------------------------------------------------------------------

int
BMarkdownView::_EnterBlockCb(MD_BLOCKTYPE type, void* detail, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);

	switch (type) {
		case MD_BLOCK_H:
		{
			MD_BLOCK_H_DETAIL* hDetail = static_cast<MD_BLOCK_H_DETAIL*>(detail);
			state->headingLevel = hDetail->level;
			break;
		}
		case MD_BLOCK_CODE:
			state->isBlockCode = true;
			state->view->Insert("\n");
			state->currentBlockStart = state->view->TextLength();
			break;
		case MD_BLOCK_TABLE:
			state->isTable = true;
			state->view->Insert("\n");
			state->currentTable = new TableRegion();
			state->currentTable->startPos = state->view->TextLength();
			break;
		case MD_BLOCK_THEAD:
			state->isHeaderRow = true;
			break;
		case MD_BLOCK_TR:
			state->currentTRStart = state->view->TextLength();
			state->view->Insert("| ");
			break;
		case MD_BLOCK_TH:
			state->isBold = true;
			break;
		case MD_BLOCK_UL: {
			state->listDepth++;
			state->isOrderedList = false;
			break;
		}
		case MD_BLOCK_OL: {
			MD_BLOCK_OL_DETAIL* olDetail = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
			state->listDepth++;
			state->isOrderedList = true;
			state->olItemNumber = (olDetail != NULL) ? olDetail->start : 1;
			break;
		}
		case MD_BLOCK_LI: {
			int32 startOffset = state->view->TextLength();

			// Rientro per liste annidate
			for (int32 i = 0; i < state->listDepth - 1; i++) {
				state->view->Insert("    ");
			}

			if (state->isOrderedList) {
				BString numStr;
				numStr.SetToFormat("%" B_PRId32 ". ", state->olItemNumber++);
				state->view->Insert(numStr.String());
			} else {
				state->view->Insert("\xE2\x80\xA2 ");
			}

			int32 endOffset = state->view->TextLength();

			rgb_color textColor = state->textColor;
			state->view->SetFontAndColor(startOffset, endOffset,
				be_plain_font, B_FONT_ALL, &textColor);
			break;
		}
		case MD_BLOCK_HR: {
    // 1. Andiamo a capo per ancorare la linea
    state->view->Insert("\n");

    // 2. Creiamo la regione per la riga orizzontale
    HorizontalRuleRegion* hr = new HorizontalRuleRegion();
    hr->pos = state->view->TextLength();
    state->view->fHorizontalRules.AddItem(hr);

    // 3. Aggiungiamo un ulteriore \n per distanziare il testo successivo
    state->view->Insert("\n");
    break;
}
		case MD_BLOCK_QUOTE: {
	QuoteRegion* quote = new QuoteRegion();
	quote->startPos = state->view->TextLength();
	quote->endPos = -1;

	state->currentQuote = quote;
	state->isQuote = true;
	break;
}
		case MD_BLOCK_P: {
	// Se siamo all'interno di una citazione, aggiungiamo del margine a sinistra prima del testo
	if (state->isQuote) {
		state->view->Insert("    "); // 4 spazi di rientro visivo dal bordo
	}
	break;
}
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
			state->view->Insert("\n");
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
		case MD_BLOCK_TABLE:
			if (state->currentTable != NULL) {
				state->currentTable->endPos = state->view->TextLength();
				state->view->fTables.AddItem(state->currentTable);
				state->currentTable = NULL;
			}
			state->isTable = false;
			state->view->Insert("\n\n");
			break;
		case MD_BLOCK_THEAD:
			state->isHeaderRow = false;
			break;
		case MD_BLOCK_TR: {
			if (state->currentTable != NULL && state->currentTRStart != -1) {
				TableRowRegion* row = new TableRowRegion();
				row->startPos = state->currentTRStart;
				row->endPos = state->view->TextLength();
				row->isHeader = state->isHeaderRow;
				state->currentTable->rows.AddItem(row);
			}
			state->currentTRStart = -1;
			state->view->Insert("\n");
			break;
		}
		case MD_BLOCK_TH:
			state->isBold = false;
			//state->view->Insert(" | ");
			state->view->Insert(" \t ");
			break;
		case MD_BLOCK_TD:
			//state->view->Insert(" | ");
			state->view->Insert(" \t ");
			break;
		case MD_BLOCK_UL:
		case MD_BLOCK_OL: {
    if (state->listDepth > 0)
        state->listDepth--;
    break;
}
		case MD_BLOCK_QUOTE: {
	if (state->currentQuote != NULL) {
		state->currentQuote->endPos = state->view->TextLength();
		state->view->Insert("\n");

		state->view->fQuotes.AddItem(state->currentQuote);
		state->currentQuote = NULL;
	}
	state->isQuote = false;
	break;
}
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
		case MD_SPAN_A: {
			MD_SPAN_A_DETAIL* aDetail = static_cast<MD_SPAN_A_DETAIL*>(detail);
			state->isLink = true;

			LinkRegion* link = new LinkRegion();
			link->startPos = state->view->TextLength();

			if (aDetail->href.text != NULL && aDetail->href.size > 0)
				link->url.SetTo(aDetail->href.text, aDetail->href.size);

			state->currentLink = link;
			break;
		}
		case MD_SPAN_STRONG:
			state->isBold = true;
			break;
		case MD_SPAN_EM:
			state->isItalic = true;
			break;
		case MD_SPAN_CODE:
			state->isCode = true;
			break;
		case MD_SPAN_IMG: {
			MD_SPAN_IMG_DETAIL* imgDetail = static_cast<MD_SPAN_IMG_DETAIL*>(detail);

	ImageRegion* imgRegion = new ImageRegion();
	// Nota: non inseriamo inserimenti di testo qui!

	if (imgDetail->src.text != NULL && imgDetail->src.size > 0)
		imgRegion->src.SetTo(imgDetail->src.text, imgDetail->src.size);

	state->view->_LoadImageForRegion(imgRegion);

	state->currentImage = imgRegion;
	state->currentImageAlt.SetTo("");
	state->isImage = true;
	break;
		}
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
		case MD_SPAN_A: {
			if (state->currentLink != NULL) {
				state->currentLink->endPos = state->view->TextLength();
				//state->view->fLinks.AddItem(state->currentLink);
				if (state->currentLink->endPos > state->currentLink->startPos) {
					state->view->fLinks.AddItem(state->currentLink);
				} else {
					delete state->currentLink;
				}
				state->currentLink = NULL;
			}
			state->isLink = false; // Disattiva lo stato link!
			break;
		}
		case MD_SPAN_STRONG:
			state->isBold = false;
			break;
		case MD_SPAN_EM:
			state->isItalic = false;
			break;
		case MD_SPAN_CODE:
			state->isCode = false;
			break;
		case MD_SPAN_IMG: {
			/*
			state->isImage = false;

	if (state->currentImage != NULL) {
		state->currentImage->alt = state->currentImageAlt;

		if (state->currentImage->bitmap != NULL && state->currentImage->bitmap->IsValid()) {
			// 1. Un solo \n prima dell'immagine per mandarla a capo pulita
			state->view->Insert("\n");
			state->currentImage->startPos = state->view->TextLength();

			float imgHeight = state->currentImage->bitmap->Bounds().Height();
			font_height fh;
			state->baseFont.GetHeight(&fh);
			float lineHeight = fh.ascent + fh.descent + fh.leading;

			if (lineHeight < 1.0f)
				lineHeight = 12.0f;

			// 2. Calcolo preciso: quante righe servono ESATTAMENTE per coprire l'altezza dell'immagine
			int32 newLinesNeeded = (int32)(imgHeight / lineHeight);
			if (newLinesNeeded < 1)
				newLinesNeeded = 1;

			for (int32 i = 0; i < newLinesNeeded; i++)
				state->view->Insert("\n");

			state->currentImage->endPos = state->view->TextLength();
			state->view->fImages.AddItem(state->currentImage);
		} else {
			// Fallback se l'immagine manca
			if (!state->currentImage->alt.IsEmpty()) {
				BString altFallback;
				altFallback.SetToFormat("[%s]", state->currentImage->alt.String());
				state->view->Insert(altFallback.String());
			}
			state->view->Insert("\n");
			delete state->currentImage;
		}
		state->currentImage = NULL;
	}
	break;*/
	state->isImage = false;

	if (state->currentImage != NULL) {
		state->currentImage->alt = state->currentImageAlt;

		if (state->currentImage->bitmap != NULL && state->currentImage->bitmap->IsValid()) {
			int32 startOffset = state->view->TextLength();
			state->view->Insert("\n");

			// 1. Dimensioni NATIVE della bitmap
			BRect bitmapBounds = state->currentImage->bitmap->Bounds();
			float nativeWidth = bitmapBounds.Width();
			float nativeHeight = bitmapBounds.Height();

			// 2. Larghezza MASSIMA disponibile nella BMarkdownView
			float viewWidth = state->view->Bounds().Width();
			float maxWidth = viewWidth - 20.0f; // Padding di sicurezza

			float renderedHeight = nativeHeight;

			// 3. SE L'IMMAGINE VIENE SCALATA, CALCOLIAMO L'ALTEZZA EFFETTIVA A SCHERMO
			if (maxWidth > 0.0f && nativeWidth > maxWidth) {
				float scale = maxWidth / nativeWidth;
				renderedHeight = nativeHeight * scale; // <- Altezza REALE disegnata
			}

			// 4. Calcoliamo la baseLineHeight neutra
			font_height fh;
			state->baseFont.GetHeight(&fh);
			float baseLineHeight = fh.ascent + fh.descent + fh.leading;
			if (baseLineHeight < 1.0f)
				baseLineHeight = 12.0f;

			// 5. Calcoliamo i \n usando 'renderedHeight' (non più nativeHeight!)
			int32 newLinesNeeded = (int32)(renderedHeight / baseLineHeight);
			if (newLinesNeeded < 1)
				newLinesNeeded = 1;

			for (int32 i = 0; i < newLinesNeeded; i++) {
				state->view->Insert("\n");
			}

			int32 lineInsertEnd = state->view->TextLength();

			// Applichiamo il font base sulle righe riservate per evitare dilatazioni
			rgb_color textColor = state->textColor;
			state->view->SetFontAndColor(startOffset, lineInsertEnd,
				&state->baseFont, B_FONT_ALL, &textColor);

			state->currentImage->startPos = startOffset;
			state->currentImage->endPos = lineInsertEnd;
			state->view->fImages.AddItem(state->currentImage);
		} else {
			if (!state->currentImage->alt.IsEmpty()) {
				BString altFallback;
				altFallback.SetToFormat("[%s]", state->currentImage->alt.String());
				state->view->Insert(altFallback.String());
			}
			state->view->Insert("\n");
			delete state->currentImage;
		}
		state->currentImage = NULL;
	}
	break;
		}
		default:
			break;
	}
	return 0;
}

int
BMarkdownView::_TextCb(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata)
{
	RenderState* state = static_cast<RenderState*>(userdata);
	
	if (state == NULL || state->view == NULL)
		return 0;
	

	// Se siamo all'interno di uno span immagine, usiamo BString(text, size)
	// per evitare problemi di puntatori non terminati da '\0'
	if (state->isImage) {
		if (text != NULL && size > 0)
			state->currentImageAlt.Append(text, size);
		return 0; // NON scriviamo l'alt text nel documento visivo!
	}
	
	int32 startPos = state->view->TextLength();
	
	BString str(text, size);
	printf("la stringa da elaborare è: %s\n",str.String());
	state->view->Insert(str.String());

	state->view->_ApplyCurrentStyle(startPos, *state);

	return 0;
}
void
BMarkdownView::_LoadImageForRegion(ImageRegion* region)
{
	if (region == NULL || region->src.IsEmpty())
		return;
		
	// Caricamento da file locale (es. /boot/home/images/photo.png o relativo)
	region->bitmap = BTranslationUtils::GetBitmap(region->src.String());

	// Se il percorso è relativo o l'immagine non è stata trovata directly
	if (region->bitmap == NULL && region->src.ByteAt(0) != '/') {
		// Tentativo c	on percorso assoluto o relativo alla directory corrente
		BPath path(region->src.String());
		region->bitmap = BTranslationUtils::GetBitmap(path.Path());
	}
	/* senza BTranslationUtils
	BFile file(region->src.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL)
		return;

	BBitmapStream stream;
	if (roster->Translate(&file, NULL, NULL, &stream, B_TRANSLATOR_BITMAP) == B_OK) {
		BBitmap* bitmap = NULL;
		if (stream.DetachBitmap(&bitmap) == B_OK) {
			region->bitmap = bitmap;
		}
	}*/
}
LinkRegion*
BMarkdownView::_LinkAt(BPoint point) const
{
	// Convertiamo le coordinate visive del punto nell'offset di testo della BTextView
	int32 offset = OffsetAt(point);
	if (offset < 0 || offset >= TextLength())
		return NULL;

	int32 linkCount = fLinks.CountItems();
	for (int32 i = 0; i < linkCount; i++) {
		LinkRegion* link = fLinks.ItemAt(i);
		if (link != NULL && offset >= link->startPos && offset < link->endPos) {
			return link;
		}
	}

	return NULL;
}

void
BMarkdownView::MouseMoved(BPoint where, uint32 transit, const BMessage* dragMessage)
{
	BTextView::MouseMoved(where, transit, dragMessage);
	// Cambiamo il cursore in una manina quando il puntatore si trova sopra un link
	LinkRegion* link = _LinkAt(where);
	if (link != NULL) {
		// Sovrascriviamo il cursore I-Beam impostato da BTextView con la manina
		SetViewCursor(&fHandCursor);
	} else if (transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW) {
		// Se non siamo su un link, usiamo il cursore di testo I-Beam
		BCursor iBeamCursor(B_CURSOR_ID_I_BEAM);
		SetViewCursor(&iBeamCursor);
	}

	//BTextView::MouseMoved(where, transit, dragMessage);
}

void
BMarkdownView::MouseDown(BPoint where)
{
	
	int32 buttons = 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);

	if (buttons == B_PRIMARY_MOUSE_BUTTON) {
		LinkRegion* link = _LinkAt(where);
		if (link != NULL && !link->url.IsEmpty()) {
			// Usiamo la classe nativa BUrl
			BUrl url(link->url.String(),true);

			if (url.IsValid()) {
				// Metodo nativo reale di BUrl per aprire il link con l'app di sistema
				status_t err = url.OpenWithPreferredApplication();
				if (err == B_OK) {
					return; // Gestito con successo!
				}
			}
		}
	}

	BTextView::MouseDown(where);
}
