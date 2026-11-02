/*
 * Copyright 2026, Fabio Tomat <f.t.public@gmail.com>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef QR_CODE_WINDOW_H
#define QR_CODE_WINDOW_H

#include <Bitmap.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <View.h>
#include <Window.h>

#include "qrcodegen.hpp" // Libreria MIT di Project Nayuki

class QRCodeView : public BView {
public:
	QRCodeView(const char* text)
		: BView("qr_view", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetExplicitMinSize(BSize(200, 200));
		SetExplicitPreferredSize(BSize(256, 256));

		// Genera il QR code
		using qrcodegen::QrCode;
		QrCode qr = QrCode::encodeText(text, QrCode::Ecc::MEDIUM);
		fSize = qr.getSize();

		// Creiamo un bitmap in scala di grigi o 32-bit
		fBitmap = new BBitmap(BRect(0, 0, fSize - 1, fSize - 1), B_GRAY8);
		uint8* bits = (uint8*)fBitmap->Bits();
		int32 bpr = fBitmap->BytesPerRow();

		for (int y = 0; y < fSize; y++) {
			for (int x = 0; x < fSize; x++) {
				bits[y * bpr + x] = qr.getModule(x, y) ? 0x00 : 0xFF;
			}
		}
	}

	~QRCodeView()
	{
		delete fBitmap;
	}

	virtual void Draw(BRect updateRect) override
	{
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(Bounds());

		if (fBitmap == NULL)
			return;

		// Mantiene l'aspetto quadrato e aggiunge un po' di margine (quiet zone)
		BRect rect = Bounds();
		rect.InsetBy(12, 12);
		float size = std::min(rect.Width(), rect.Height());
		rect.right = rect.left + size;
		rect.bottom = rect.top + size;

		// Disegna con interpolazione disattivata per mantenere i pixel nitidi
		SetDrawingMode(B_OP_COPY);
		DrawBitmap(fBitmap, fBitmap->Bounds(), rect, 0);
	}

private:
	BBitmap* fBitmap;
	int fSize;
};


class QRCodeWindow : public BWindow {
public:
	QRCodeWindow(const char* ssid, const char* password, const char* authType = "WPA")
		: BWindow(BRect(0, 0, 320, 380), "Condividi rete Wi-Fi",
			B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
	{
		// Formato standard Wi-Fi QR code
		BString qrText;
		qrText.SetToFormat("WIFI:S:%s;T:%s;P:%s;;", ssid, authType, password);

		QRCodeView* qrView = new QRCodeView(qrText.String());
		
		BString passwordText;
		passwordText.SetToFormat("Password: %s", password);
		BStringView* passLabel = new BStringView("pass_label", passwordText.String());
		passLabel->SetAlignment(B_ALIGN_CENTER);

		BButton* closeButton = new BButton("close", "Chiudi", new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_DEFAULT_SPACING)
			.Add(qrView)
			.Add(passLabel)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(closeButton)
			.End();

		CenterOnScreen();
	}
};

#endif // QR_CODE_WINDOW_H
