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
#include <Clipboard.h>

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
			TableRowRegion* firstRow = table->rows.ItemAt(0);
			TableRowRegion* lastRow  = table->rows.ItemAt(rowCount - 1);

			BPoint startPt = PointAt(firstRow->startPos);
			int32 lastPosAdjusted = std::max(lastRow->startPos, lastRow->endPos - 1);
			BPoint endPt = PointAt(lastPosAdjusted);

			BRect totalTableRect;
			totalTableRect.left = 10.0f; // Padding di 10px dal bordo sinistro per evitare sovrapposizioni
			totalTableRect.right = Bounds().Width() - 10.0f;
			totalTableRect.top = startPt.y - 2.0f;
			totalTableRect.bottom = endPt.y + LineHeight(lastPosAdjusted) + 2.0f;

			if (!totalTableRect.Intersects(updateRect))
				continue;

			// 1. ZEBRA STRIPING DELLE RIGHE (Sfondo e linee orizzontali)
			for (int32 r = 0; r < rowCount; r++) {
				TableRowRegion* row = table->rows.ItemAt(r);
				BPoint rStartPt = PointAt(row->startPos);
				int32 rEndAdjusted = std::max(row->startPos, row->endPos - 1);

				BRect rowRect;
				rowRect.left = totalTableRect.left;
				rowRect.right = totalTableRect.right;
				rowRect.top = rStartPt.y - 2.0f;
				rowRect.bottom = rStartPt.y + LineHeight(rEndAdjusted) + 2.0f;

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

				// Linea orizzontale sotto ogni riga
				SetHighColor(tableBorderColor);
				StrokeLine(BPoint(rowRect.left, rowRect.bottom), BPoint(rowRect.right, rowRect.bottom));
			}

			// 2. RENDERING DELLE LINEE VERTICALI DIVISORIE
			int32 maxCols = 0;
			for (int32 r = 0; r < rowCount; r++) {
				TableRowRegion* row = table->rows.ItemAt(r);
				if (row != NULL && row->cells.CountItems() > maxCols)
					maxCols = row->cells.CountItems();
			}

			if (maxCols > 1) {
				float colWidth = totalTableRect.Width() / (float)maxCols;
				SetHighColor(tableBorderColor);

				for (int32 c = 1; c < maxCols; c++) {
					float xLine = totalTableRect.left + (c * colWidth);
					StrokeLine(
						BPoint(xLine, totalTableRect.top),
						BPoint(xLine, totalTableRect.bottom)
					);
				}
			}

			// 3. BORDO ESTERNO ARROTONDATO DELL'INTERA TABELLA
			SetHighColor(tableBorderColor);
			StrokeRoundRect(totalTableRect, 4.0f, 4.0f);

			// 4. RIDISEGNO DEL TESTO IN OVERLAY CON PADDING
			SetDrawingMode(B_OP_OVER);
			SetFont(be_fixed_font);

			float colWidth = (maxCols > 0) ? (totalTableRect.Width() / (float)maxCols) : totalTableRect.Width();

			for (int32 r = 0; r < rowCount; r++) {
				TableRowRegion* row = table->rows.ItemAt(r);
				int32 cellCount = row->cells.CountItems();

				for (int32 c = 0; c < cellCount; c++) {
					TableCellRegion* cell = row->cells.ItemAt(c);
					if (cell == NULL || cell->text.IsEmpty())
						continue;

					BPoint linePt = PointAt(row->startPos);

					// Incolonnamento con padding di 10px dal bordo sinistro della colonna
					float cellX = totalTableRect.left + (c * colWidth) + 10.0f;

					if (row->isHeader)
						SetFont(be_bold_font);
					else
						SetFont(be_fixed_font);

					SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
					DrawString(cell->text.String(), BPoint(cellX, linePt.y + LineHeight(row->startPos) - 3.0f));
				}
			}
		}
	}
	
	// if there's no codeblocks just exit!
	int32 count = fCodeBlocks.CountItems();
	if (count > 0) {
		rgb_color blockBgColor  = (luminance >= 128.0f) ? (rgb_color){ 35, 38, 41, 255 } : (rgb_color){ 245, 242, 220, 255 };
		rgb_color headerBgColor = (luminance >= 128.0f) ? (rgb_color){ 28, 30, 33, 255 } : (rgb_color){ 230, 227, 205, 255 };
		rgb_color codeTextColor = (luminance >= 128.0f) ? (rgb_color){ 235, 238, 242, 255 } : (rgb_color){ 25, 25, 25, 255 };
		rgb_color borderColor   = (luminance >= 128.0f) ? (rgb_color){ 60, 65, 70, 255 } : (rgb_color){ 210, 205, 180, 255 };
		
		font_height fh;
		be_fixed_font->GetHeight(&fh);
		float lineHeight = fh.ascent + fh.descent + fh.leading;

		for (int32 i = 0; i < count; i++) {
			CodeBlockRegion* block = fCodeBlocks.ItemAt(i);
			if (block == NULL || block->codeText.IsEmpty())
				continue;

			int32 totalLines = 0;
			int32 strPos = 0;
			int32 codeLen = block->codeText.Length();
			while (strPos < codeLen) {
				totalLines++;
				int32 lineEnd = block->codeText.FindFirst('\n', strPos);
				if (lineEnd == B_ERROR)
					break;
				strPos = lineEnd + 1;
			}
			if (totalLines < 1) totalLines = 1;

			// 2. Calcoliamo la coordinata di partenza top basandoci su startPos
			BPoint startPt = PointAt(block->startPos);
			float headerHeight = 22.0f;
			float codeHeight = totalLines * lineHeight;
			float totalBlockHeight = headerHeight + codeHeight + 12.0f; // 12px di padding globale

			BRect blockRect;
			blockRect.left = 2.0f;
			blockRect.right = Bounds().Width() - 2.0f;
			blockRect.top = startPt.y - headerHeight - 4.0f; // Fa salire il riquadro per racchiudere l'header
			blockRect.bottom = blockRect.top + totalBlockHeight;

			if (blockRect.Intersects(updateRect)) {
				// A. Sfondo del riquadro principale
				SetDrawingMode(B_OP_COPY);
				SetHighColor(blockBgColor);
				FillRoundRect(blockRect, 4.0f, 4.0f);

				// B. Barra d'intestazione superiore (Header)
				BRect headerRect(blockRect.left, blockRect.top, blockRect.right, blockRect.top + headerHeight);
				SetHighColor(headerBgColor);
				FillRoundRect(headerRect, 4.0f, 4.0f);

				// Linea di separazione sotto l'header
				SetHighColor(borderColor);
				StrokeLine(BPoint(headerRect.left, headerRect.bottom), BPoint(headerRect.right, headerRect.bottom));

				// Bordo esterno arrotondato
				StrokeRoundRect(blockRect, 4.0f, 4.0f);

				// C. Pulsante "Copia"
				SetDrawingMode(B_OP_OVER);
				SetHighColor(codeTextColor);
				SetFont(be_plain_font);

				const char* copyStr = "📑 Copy";
				float copyWidth = StringWidth(copyStr);

				block->copyRect.Set(
					headerRect.right - copyWidth - 12.0f,
					headerRect.top + 2.0f,
					headerRect.right - 4.0f,
					headerRect.bottom - 2.0f
				);

				DrawString(copyStr, BPoint(block->copyRect.left + 2.0f, headerRect.top + 15.0f));

				// D. Testo del codice sorgente
				SetFont(be_fixed_font);
				const float codeLeftPadding = 10.0f;
				float currentY = headerRect.bottom + 6.0f;

				strPos = 0;
				while (strPos < codeLen) {
					int32 lineEnd = block->codeText.FindFirst('\n', strPos);
					if (lineEnd == B_ERROR)
						lineEnd = codeLen;

					BString lineStr;
					block->codeText.CopyInto(lineStr, strPos, lineEnd - strPos);

					DrawString(lineStr.String(), BPoint(blockRect.left + codeLeftPadding, currentY + fh.ascent));

					currentY += lineHeight;
					strPos = lineEnd + 1;
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
	
	SetFontAndColor(be_plain_font, B_FONT_ALL);
	state.currentFont = *be_plain_font;
	
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
		state.currentFont.SetSize(be_plain_font->Size() * factor);
	} else {
		state.currentFont.SetSize(be_plain_font->Size());
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
	if (state == NULL || state->view == NULL)
		return 0;

	switch (type) {
		case MD_BLOCK_H: {
			MD_BLOCK_H_DETAIL* hDetail = static_cast<MD_BLOCK_H_DETAIL*>(detail);
			state->headingLevel = hDetail->level;
			break;
		}
		case MD_BLOCK_CODE:
		{
			state->isBlockCode = true;
			state->view->Insert("\n\n"); // Riga riservata per l'header della toolbar
			CodeBlockRegion* region = new CodeBlockRegion();
			region->startPos = state->view->TextLength();
			region->endPos = -1;
			state->currentCodeBlock = region;
			break;
		}
		case MD_BLOCK_TABLE:
			state->isTable = true;
			state->view->Insert("\n");
			state->currentTable = new TableRegion();
			state->currentTable->startPos = state->view->TextLength();
			break;

		case MD_BLOCK_THEAD:
			state->isHeaderRow = true;
			break;

		case MD_BLOCK_TR: {
			if (state->currentTable != NULL) {
				TableRowRegion* row = new TableRowRegion();
				row->startPos = state->view->TextLength();
				row->isHeader = state->isHeaderRow;
				state->currentTable->rows.AddItem(row);
				state->currentColIndex = 0;
			}
			break;
		}
		case MD_BLOCK_TH:
			state->isBold = true;
			// fall-through intenzionale verso MD_BLOCK_TD
		case MD_BLOCK_TD: {
	if (state->currentTable != NULL && state->currentTable->rows.CountItems() > 0) {
		TableRowRegion* row = state->currentTable->rows.LastItem();

		// Inseriamo un tab per separare fisicamente le colonne nel buffer di BTextView
		//if (state->currentColIndex > 0) {
		//	state->view->Insert("\t");
		//}

		TableCellRegion* cell = new TableCellRegion();
		cell->startPos = state->view->TextLength();
		cell->colIndex = state->currentColIndex;
		row->cells.AddItem(cell);
		state->currentCell = cell;
	}
	break;
}
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
			state->view->Insert("\n");

			HorizontalRuleRegion* hr = new HorizontalRuleRegion();
			hr->pos = state->view->TextLength();
			state->view->fHorizontalRules.AddItem(hr);

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
			if (state->isQuote) {
				state->view->Insert("    ");
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
	if (state == NULL || state->view == NULL)
		return 0;

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
			if (state->currentCodeBlock != NULL) {
				state->currentCodeBlock->endPos = state->view->TextLength();
				state->view->fCodeBlocks.AddItem(state->currentCodeBlock);
				state->currentCodeBlock = NULL;
			}

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

		case MD_BLOCK_TH:
			state->isBold = false;
			// fall-through intenzionale verso MD_BLOCK_TD
		case MD_BLOCK_TD:
			if (state->currentCell != NULL) {
				state->currentCell->endPos = state->view->TextLength();
				state->currentCell = NULL;
			}
			state->currentColIndex++;
			break;

		case MD_BLOCK_TR: {
			if (state->currentTable != NULL && state->currentTable->rows.CountItems() > 0) {
				TableRowRegion* row = state->currentTable->rows.LastItem();
				row->endPos = state->view->TextLength();
				state->view->Insert("\n");
			}
			break;
		}

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
			be_plain_font->GetHeight(&fh);
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
				be_plain_font, B_FONT_ALL, &textColor);

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
	// Se siamo dentro una cella di una tabella, salviamo il testo NELLA CELLA
	// e NON inseriamo il testo grezzo nella BTextView!
	if (state->isTable && state->currentCell != NULL) {
		if (text != NULL && size > 0) {
			state->currentCell->text.Append(text, size);
		}
		return 0; // Impedisce a BTextView di inserire il testo visibile a margine!
	}
	
	if (state->isBlockCode) {
		if (text != NULL && size > 0) {
			// Per mantenere l'altezza verticale corretta nella BTextView,
			// inseriamo SOLO i caratteri '\n' di a-capo nel buffer di BTextView
			for (MD_SIZE i = 0; i < size; i++) {
				if (text[i] == '\n')
					state->view->Insert("\n");
			}
			// Salviamo il testo del codice grezzo nella regione
			if (state->currentCodeBlock != NULL) {
				state->currentCodeBlock->codeText.Append(text, size);
			}
		}
		return 0; // Impedisce l'inserimento del testo grezzo nella BTextView!
	}
	
	int32 startPos = state->view->TextLength();
	
	BString str(text, size);
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

	bool overInteractiveElement = false;

	// 1. Check passaggio su pulsante "Copia" nei CodeBlock
	int32 codeCount = fCodeBlocks.CountItems();
	for (int32 i = 0; i < codeCount; i++) {
		CodeBlockRegion* block = fCodeBlocks.ItemAt(i);
		if (block != NULL && block->copyRect.Contains(where)) {
			overInteractiveElement = true;
			break;
		}
	}

	// 2. Check passaggio su pulsante "Copia" nelle Tabelle
	if (!overInteractiveElement) {
		int32 tableCount = fTables.CountItems();
		for (int32 t = 0; t < tableCount; t++) {
			TableRegion* table = fTables.ItemAt(t);
			if (table != NULL && table->copyRect.Contains(where)) {
				overInteractiveElement = true;
				break;
			}
		}
	}

	// 3. Check passaggio su un Link
	if (!overInteractiveElement && _LinkAt(where) != NULL) {
		overInteractiveElement = true;
	}

	// Gestione dinamica del cursore
	if (overInteractiveElement) {
		SetViewCursor(&fHandCursor);
	} else if (transit == B_INSIDE_VIEW || transit == B_ENTERED_VIEW) {
		BCursor iBeamCursor(B_CURSOR_ID_I_BEAM);
		SetViewCursor(&iBeamCursor);
	}
}
void
BMarkdownView::MouseDown(BPoint where)
{
	
	int32 buttons = 0;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);

	if (buttons == B_PRIMARY_MOUSE_BUTTON) {
		int32 codeCount = fCodeBlocks.CountItems();
		for (int32 i = 0; i < codeCount; i++) {
			CodeBlockRegion* block = fCodeBlocks.ItemAt(i);
			if (block != NULL && block->copyRect.Contains(where)) {
				if (be_clipboard->Lock()) {
					be_clipboard->Clear();
					BMessage* clip = be_clipboard->Data();
					if (clip != NULL) {
						clip->AddData("text/plain", B_MIME_TYPE,
							block->codeText.String(), block->codeText.Length());
						be_clipboard->Commit();
					}
					be_clipboard->Unlock();
				}
				return; // Gestito!
			}
		}

		// 2. Controllo click sul pulsante "Copia" delle Tabelle
		int32 tableCount = fTables.CountItems();
		for (int32 t = 0; t < tableCount; t++) {
			TableRegion* table = fTables.ItemAt(t);
			if (table != NULL && table->copyRect.Contains(where)) {
				BString tableText = table->ToText();
				if (!tableText.IsEmpty() && be_clipboard->Lock()) {
					be_clipboard->Clear();
					BMessage* clip = be_clipboard->Data();
					if (clip != NULL) {
						clip->AddData("text/plain", B_MIME_TYPE,
							tableText.String(), tableText.Length());
						be_clipboard->Commit();
					}
					be_clipboard->Unlock();
				}
				return; // Gestito!
			}
		}
		
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
