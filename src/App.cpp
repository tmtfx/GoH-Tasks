#include "App.h"
#include "MainWindow.h"

#include <Alert.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <TextControl.h>
#include <StringView.h>
#include <Locker.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Rect.h>
#include <Font.h>
#include "Localization.h"

#include <cstring>

#include "GTasks-icon.h"

// ============================================================
//  Setup Dialog — shown when no credentials are configured
// ============================================================

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SetupWindow"

static const uint32 MSG_SETUP_SAVE   = 'sups';
static const uint32 MSG_SETUP_CANCEL = 'supc';

class SetupWindow : public BWindow {
public:
    SetupWindow()
        : BWindow(BRect(0, 0, 560, 300), B_TRANSLATE("GoH-Tasks Setup"),
                  B_MODAL_WINDOW,
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
          fSaved(false), fDone(false)
    {
        BView* bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
        bg->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        AddChild(bg);

        float x = 14, y = 14, w = Bounds().Width() - 28;

        BStringView* instr1 = new BStringView(
            BRect(x, y, x + w, y + 20), "i1",
            B_TRANSLATE("GoH-Tasks needs OAuth 2.0 credentials from Google Cloud Console."));
        bg->AddChild(instr1);
        y += 24;

        BStringView* instr2 = new BStringView(
            BRect(x, y, x + w, y + 20), "i2",
            B_TRANSLATE("Go to: console.cloud.google.com -> APIs & Services -> Credentials"));
        bg->AddChild(instr2);
        y += 24;

        BStringView* instr3 = new BStringView(
            BRect(x, y, x + w, y + 20), "i3",
            B_TRANSLATE("Create an OAuth 2.0 Client ID (Desktop application type)."));
        bg->AddChild(instr3);
        y += 24;

        BStringView* instr4 = new BStringView(
            BRect(x, y, x + w, y + 20), "i4",
            B_TRANSLATE("Enable the Google Tasks API for your project first."));
        bg->AddChild(instr4);
        y += 34;

        fClientIdField = new BTextControl(
            BRect(x, y, x + w, y + 24), "client_id",
            B_TRANSLATE("Client ID:"), "", nullptr);
        fClientIdField->SetDivider(90);
        bg->AddChild(fClientIdField);
        y += 30;

        fClientSecretField = new BTextControl(
            BRect(x, y, x + w, y + 24), "client_secret",
            B_TRANSLATE("Client Secret:"), "", nullptr);
        fClientSecretField->SetDivider(90);
        bg->AddChild(fClientSecretField);
        y += 36;

        float btnW = 110;
        float btnH = 26;
        float btnY = Bounds().Height() - btnH - 20;//14

        BButton* cancelBtn = new BButton(
            BRect(x, btnY, x + btnW, btnY + btnH),
            "cancel", B_TRANSLATE("Cancel"), new BMessage(MSG_SETUP_CANCEL));
        bg->AddChild(cancelBtn);

        BButton* saveBtn = new BButton(
            BRect(Bounds().Width() - x - btnW, btnY,
                  Bounds().Width() - x, btnY + btnH),
            "save", B_TRANSLATE("Save & Continue"), new BMessage(MSG_SETUP_SAVE));
        saveBtn->MakeDefault(true);
        bg->AddChild(saveBtn);

        CenterOnScreen();
    }

    void MessageReceived(BMessage* msg) override {
        switch (msg->what) {
        case MSG_SETUP_SAVE: {
            const char* id     = fClientIdField->Text();
            const char* secret = fClientSecretField->Text();
            if (!id || strlen(id) == 0 || !secret || strlen(secret) == 0) {
                BAlert* alert = new BAlert(B_TRANSLATE("Missing Fields"),
                                           B_TRANSLATE("Please enter both Client ID and Client Secret."),
                                           B_TRANSLATE("OK"), nullptr, nullptr,
                                           B_WIDTH_AS_USUAL, B_WARNING_ALERT);
                alert->Go();
                break;
            }
            fResultId     = id;
            fResultSecret = secret;
            fSaved        = true;
            fDone         = true;
            Quit();
            break;
        }
        case MSG_SETUP_CANCEL:
            fDone = true;
            Quit();
            break;
        default:
            BWindow::MessageReceived(msg);
        }
    }

    bool QuitRequested() override {
        fDone = true;
        return true;
    }

    // Block until window is done
    void WaitForClose() {
        while (!fDone)
            snooze(50000); // 50ms
    }

    bool     fSaved;
    volatile bool fDone;
    std::string fResultId;
    std::string fResultSecret;

private:
    BTextControl* fClientIdField;
    BTextControl* fClientSecretField;
};

// ============================================================
//  About Window
// ============================================================

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AboutWindow"

class LogoView : public BView {
public:
    LogoView(BRect frame)
        : BView(frame, "logo", B_FOLLOW_NONE, B_WILL_DRAW)
    {
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        fBitmap = new BBitmap(BRect(0, 0, kBitmapWidth - 1, kBitmapHeight - 1),
                              kBitmapFormat);
        fBitmap->SetBits(kGTasksIconBits, sizeof(kGTasksIconBits), 0, kBitmapFormat);
    }

    ~LogoView() override { delete fBitmap; }

    void Draw(BRect updateRect) override {
        BView::Draw(updateRect);
        if (fBitmap) {
            SetDrawingMode(B_OP_ALPHA);
            SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
            DrawBitmap(fBitmap, Bounds());
            SetDrawingMode(B_OP_COPY);
        }
    }

private:
    BBitmap* fBitmap;
};

class AboutWindow : public BWindow {
public:
    AboutWindow()
        : BWindow(BRect(0, 0, 320, 300), B_TRANSLATE("About GoH-Tasks"),
                  B_MODAL_WINDOW,
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS)
    {
        BView* bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
        bg->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        AddChild(bg);

        float logoSize = 96.0f;
        float x = (Bounds().Width() - logoSize) / 2.0f;
        LogoView* logo = new LogoView(BRect(x, 16, x + logoSize, 16 + logoSize));
        bg->AddChild(logo);

        float y = 16 + logoSize + 10;
        BStringView* name = new BStringView(
            BRect(0, y, Bounds().Width(), y + 20), "name",
            B_TRANSLATE_SYSTEM_NAME("GoH-Tasks"));
        name->SetAlignment(B_ALIGN_CENTER);
        BFont boldFont(be_bold_font);
        boldFont.SetSize(14);
        name->SetFont(&boldFont);
        bg->AddChild(name);
        y += 30;

        BStringView* tagline = new BStringView(
            BRect(0, y, Bounds().Width(), y + 24), "tagline",
            B_TRANSLATE("A Google Tasks client for Haiku"));
        tagline->SetAlignment(B_ALIGN_CENTER);
        bg->AddChild(tagline);
        y += 30;

        float btnW = 80, btnH = 26;
        BButton* okBtn = new BButton(
            BRect((Bounds().Width() - btnW) / 2.0f, Bounds().Height() - btnH - 20,
                  (Bounds().Width() - btnW) / 2.0f + btnW, Bounds().Height() - 20),
            "ok", B_TRANSLATE("OK"), new BMessage(B_QUIT_REQUESTED));
        okBtn->MakeDefault(true);
        bg->AddChild(okBtn);

        CenterOnScreen();
    }

    bool QuitRequested() override { return true; }
};

// ============================================================
//  App
// ============================================================

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "App"

App::App()
    : BApplication("application/x-vnd.GoH-Tasks"),
      fAuth(nullptr), fAPI(nullptr), fMainWindow(nullptr)
{
    InitAppCatalog();

    fAuth = new GoogleAuth();
    fAPI  = new GoogleTasksAPI(fAuth);
}

App::~App()
{
    delete fAPI;
    delete fAuth;
}

void App::ReadyToRun()
{
    // 1. Load stored client credentials
    fAuth->loadConfig();

    // 2. If no credentials, show setup dialog
    if (!fAuth->hasCredentials()) {
        ShowSetupDialog();
    }

    // 3. Still no credentials → quit
    if (!fAuth->hasCredentials()) {
        BAlert* alert = new BAlert(B_TRANSLATE("Setup Required"),
                                   B_TRANSLATE("No credentials configured. "
                                   "GoH-Tasks cannot run without OAuth credentials."),
                                   B_TRANSLATE("OK"), nullptr, nullptr,
                                   B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->Go();
        PostMessage(B_QUIT_REQUESTED);
        return;
    }

    // 4. Create and show main window
    fMainWindow = new MainWindow(fAuth, fAPI);
    fMainWindow->Show();

    // 5. Trigger authentication if not already authenticated
    if (!fAuth->isAuthenticated()) {
        // Authenticate in a background thread so we don't block the UI
        struct AuthData {
            GoogleAuth*  auth;
            BLooper*     looper;
        };
        AuthData* ad = new AuthData;
        ad->auth   = fAuth;
        ad->looper = fMainWindow;

        resume_thread(spawn_thread([](void* data) -> int32 {
            AuthData* ad = static_cast<AuthData*>(data);
            bool ok = ad->auth->authenticate();
            if (ok) {
                // Post refresh message to load task lists
                BMessage m(MSG_REFRESH);
                ad->looper->PostMessage(&m);
            } else {
                BMessage m(MSG_ERROR);
                m.AddString("error", B_TRANSLATE("Authentication failed. Please check credentials."));
                ad->looper->PostMessage(&m);
            }
            delete ad;
            return 0;
        }, "auth_thread", B_NORMAL_PRIORITY, ad));
    } else {
        // Already authenticated — load task lists
        fMainWindow->PostMessage(MSG_REFRESH);
    }
}

void App::ShowSetupDialog()
{
    SetupWindow* win = new SetupWindow();
    win->Show();
    win->WaitForClose();

    if (win->fSaved) {
        fAuth->saveConfig(win->fResultId, win->fResultSecret);
    }
    // win destroyed itself via Quit()
}

void App::AboutRequested()
{
    AboutWindow* win = new AboutWindow();
    win->Show();
}

void App::MessageReceived(BMessage* msg)
{
    switch (msg->what) {
    case MSG_OPEN_PREFS:
        ShowSetupDialog();
        break;
    default:
        BApplication::MessageReceived(msg);
        break;
    }
}

// ============================================================
//  main
// ============================================================

int main()
{
    App app;
    app.Run();
    return 0;
}
