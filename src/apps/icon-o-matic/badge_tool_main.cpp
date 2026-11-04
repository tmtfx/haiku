// Scratch one-off tool: add a green checkmark badge shape to an
// Icon-O-Matic native (.iom) icon file, reusing the real Icon-O-Matic
// domain classes for full round-trip fidelity. Not part of the shipped
// app - meant to be built, run once, then removed.

#include <stdio.h>
#include <string.h>

#include <File.h>
#include <Message.h>

#include "Container.h"
#include "Icon.h"
#include "MessageExporter.h"
#include "MessageImporter.h"
#include "PathSourceShape.h"
#include "Style.h"
#include "VectorPath.h"


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <in.iom> [out.iom]\n", argv[0]);
		return 1;
	}

	const char* inPath = argv[1];
	const char* outPath = argc > 2 ? argv[2] : NULL;

	BFile inFile(inPath, B_READ_ONLY);
	status_t err = inFile.InitCheck();
	if (err != B_OK) {
		fprintf(stderr, "failed to open '%s': %s\n", inPath, strerror(err));
		return 1;
	}

	Icon icon;
	MessageImporter importer;
	err = importer.Import(&icon, &inFile);
	if (err != B_OK) {
		fprintf(stderr, "import failed: %s\n", strerror(err));
		return 1;
	}

	printf("paths: %ld, styles: %ld, shapes: %ld\n",
		(long)icon.Paths()->CountItems(), (long)icon.Styles()->CountItems(),
		(long)icon.Shapes()->CountItems());

	BRect overall;
	for (int32 i = 0; i < icon.Paths()->CountItems(); i++) {
		VectorPath* p = icon.Paths()->ItemAtFast(i);
		BRect b = p->Bounds();
		printf("  path %ld: %ld points, bounds (%.2f,%.2f)-(%.2f,%.2f)\n",
			(long)i, (long)p->CountPoints(), b.left, b.top, b.right, b.bottom);
		overall = overall.IsValid() ? (overall | b) : b;
	}
	printf("overall bounds: (%.2f,%.2f)-(%.2f,%.2f)\n",
		overall.left, overall.top, overall.right, overall.bottom);

	if (outPath == NULL)
		return 0;

	// Checkmark badge: a small filled ribbon-shaped tick, placed as a
	// bottom-right badge over roughly the lower-right quarter of the
	// icon's own overall bounds, so it scales with whatever canvas this
	// icon actually uses instead of a hardcoded guess.
	float w = overall.Width();
	float h = overall.Height();
	float cx = overall.left + w * 0.72f;
	float cy = overall.top + h * 0.72f;
	float s = (w < h ? w : h) * 0.26f;	// badge half-size

	rgb_color green = {46, 160, 67, 255};
	Style* style = new Style(green);
	icon.Styles()->AddItem(style);

	VectorPath* path = new VectorPath();
	// Checkmark ribbon, drawn in a local -1..1 unit square, scaled by s
	// and centered at (cx, cy). Outer tick outline, clockwise, as a
	// thin "v" with some width so it fills solid instead of needing a
	// stroke transformer.
	struct { float x, y; } pts[] = {
		{-0.70f,  0.05f}, {-0.35f,  0.40f}, {-0.15f,  0.20f},
		{ 0.65f, -0.55f}, { 0.80f, -0.40f}, {-0.15f,  0.55f},
		{-0.35f,  0.55f}, {-0.85f,  0.20f},
	};
	for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++)
		path->AddPoint(BPoint(cx + pts[i].x * s, cy + pts[i].y * s));
	path->SetClosed(true);
	icon.Paths()->AddItem(path);

	PathSourceShape* shape = new PathSourceShape(style);
	shape->Paths()->AddItem(path);
	icon.Shapes()->AddItem(shape);

	BFile outFile(outPath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	err = outFile.InitCheck();
	if (err != B_OK) {
		fprintf(stderr, "failed to open '%s' for writing: %s\n",
			outPath, strerror(err));
		return 1;
	}

	MessageExporter exporter;
	err = exporter.Export(&icon, &outFile);
	if (err != B_OK) {
		fprintf(stderr, "export failed: %s\n", strerror(err));
		return 1;
	}

	printf("wrote '%s' with checkmark badge added.\n", outPath);
	return 0;
}
