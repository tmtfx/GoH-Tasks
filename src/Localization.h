#pragma once
#include <Catalog.h>
#include <String.h>

// BLocaleRoster::Default()->GetCatalog() (what B_TRANSLATE() uses by
// default) resolves the catalog for "the calling image" via its entry_ref,
// which needs the full locale-kit catalog add-on machinery. In some
// environments that negotiation silently fails (falls back to the
// untranslated source strings) even though a matching, correctly attributed
// .catalog file is installed and perfectly loadable by hand.
//
// BCatalog::SetTo(signature, language) — the simple, explicit lookup — does
// not depend on that machinery and works reliably. InitAppCatalog() uses it
// to build our own catalog from the system's preferred language list, and
// B_CATALOG is overridden (the hook Catalog.h provides for exactly this) so
// every B_TRANSLATE() call in the app goes through it instead.

extern BCatalog gAppCatalog;
void InitAppCatalog();

#undef B_CATALOG
#define B_CATALOG (&gAppCatalog)
