#include "Localization.h"

#include <LocaleRoster.h>
#include <Message.h>

BCatalog gAppCatalog;

void InitAppCatalog()
{
    BMessage preferred;
    if (BLocaleRoster::Default()->GetPreferredLanguages(&preferred) != B_OK)
        return;

    BString lang;
    for (int32 i = 0; preferred.FindString("language", i, &lang) == B_OK; ++i) {
        if (gAppCatalog.SetTo("application/x-vnd.GoH-Tasks", lang.String()) == B_OK
            && gAppCatalog.InitCheck() == B_OK) {
            return;
        }
    }
    // No catalog matched any preferred language — gAppCatalog stays
    // uninitialized, and BCatalog::GetString() safely returns the
    // untranslated (English) source string in that case.
}
