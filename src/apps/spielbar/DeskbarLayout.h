/*
 * SpielBar v3 - posizione della finestra rispetto alla Deskbar
 *
 * Generato da tools/split_app.py a partire da App.cpp (logica invariata).
 */

#ifndef SPIELBAR_DESKBARLAYOUT_H
#define SPIELBAR_DESKBARLAYOUT_H

#include <Point.h>
#include <Rect.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SpielBar"


// ---- Posizione rispetto alla Deskbar -----------------------------------
//
// La Deskbar ha tre aspetti (espansa, compatta, mini) e BDeskbar espone solo
// un booleano "expanded", che non basta. Lo stesso algoritmo dello script
// bash+hey, con le stesse misure ricavate via scripting:
//   - orizzontale: x == 0 -> sinistra (o a tutta larghezza se arriva al
//     bordo destro dello schermo), altrimenti destra;
//   - verticale:   y == 0 -> in alto, altrimenti in basso;
//   - riga o colonna: la "shelf" (View 1 of View 0 of Window 0, la tray)
//     con top == 0 sta sulla stessa riga del menu, quindi la barra e' una
//     riga (mini, o espansa in alto/in basso); altrimenti e' una colonna
//     (compatta, o espansa ai lati).
// Riga: la finestra va sopra/sotto la barra. Colonna: accanto alla barra.
// In entrambi i casi si appoggia al bordo esterno dello schermo.

BRect
GetDeskbarFrame();

// Top della tray (shelf) nella view della Deskbar; false se lo scripting
// non risponde.

bool
GetDeskbarShelfTop(float* top);

// true se la Deskbar sta in alto: la finestra le si appoggia sotto, quindi i
// comandi vanno in alto; altrimenti la finestra sta sopra e i comandi in basso.

bool
IsDeskbarAtTop();

BPoint
CalculateAppPosition(BRect windowRect);

// -------------------------------------------------------------
// Finestra
// -------------------------------------------------------------

// Frame iniziale (fuori schermo: lo posiziona CalculateAppPosition): larghezza
// dal font, altezza dal font e dall'ultima altezza scelta dall'utente.

BRect
InitialWindowFrame();

#endif	// SPIELBAR_DESKBARLAYOUT_H
