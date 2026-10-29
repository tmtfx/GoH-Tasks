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

#include <cstdio>
#include <cstring>

// ============================================================
//  Setup Dialog — shown when no credentials are configured
// ============================================================

static const uint32 MSG_SETUP_SAVE   = 'sups';
static const uint32 MSG_SETUP_CANCEL = 'supc';

class SetupWindow : public BWindow {
public:
    SetupWindow()
        : BWindow(BRect(0, 0, 440, 260), "GoH-Tasks Setup",
                  B_MODAL_WINDOW,
                  B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
          fSaved(false), fDone(false)
    {
        BView* bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
        bg->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        AddChild(bg);

        float x = 12, y = 12, w = Bounds().Width() - 24;

        BStringView* instr1 = new BStringView(
            BRect(x, y, x + w, y + 18), "i1",
            "GoH-Tasks needs OAuth 2.0 credentials from Google Cloud Console.");
        bg->AddChild(instr1);
        y += 22;

        BStringView* instr2 = new BStringView(
            BRect(x, y, x + w, y + 18), "i2",
            "Go to: console.cloud.google.com → APIs & Services → Credentials");
        bg->AddChild(instr2);
        y += 22;

        BStringView* instr3 = new BStringView(
            BRect(x, y, x + w, y + 18), "i3",
            "Create an OAuth 2.0 Client ID (Desktop application type).");
        bg->AddChild(instr3);
        y += 22;

        BStringView* instr4 = new BStringView(
            BRect(x, y, x + w, y + 18), "i4",
            "Enable the Google Tasks API for your project first.");
        bg->AddChild(instr4);
        y += 30;

        fClientIdField = new BTextControl(
            BRect(x, y, x + w, y + 24), "client_id",
            "Client ID:", "", nullptr);
        fClientIdField->SetDivider(90);
        bg->AddChild(fClientIdField);
        y += 30;

        fClientSecretField = new BTextControl(
            BRect(x, y, x + w, y + 24), "client_secret",
            "Client Secret:", "", nullptr);
        fClientSecretField->SetDivider(90);
        bg->AddChild(fClientSecretField);
        y += 36;

        float btnW = 110;
        float btnH = 26;
        float btnY = Bounds().Height() - btnH - 14;

        BButton* cancelBtn = new BButton(
            BRect(x, btnY, x + btnW, btnY + btnH),
            "cancel", "Cancel", new BMessage(MSG_SETUP_CANCEL));
        bg->AddChild(cancelBtn);

        BButton* saveBtn = new BButton(
            BRect(Bounds().Width() - x - btnW, btnY,
                  Bounds().Width() - x, btnY + btnH),
            "save", "Save & Continue", new BMessage(MSG_SETUP_SAVE));
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
                BAlert* alert = new BAlert("Missing Fields",
                                           "Please enter both Client ID and Client Secret.",
                                           "OK", nullptr, nullptr,
                                           B_WIDTH_AS_USUAL, B_WARNING_ALERT);
                alert->Go();
                break;
            }
            fResultId     = id;
            fResultSecret = secret;
            fSaved        = true;
            fDone         = true;
            Lock();
            Quit();
            break;
        }
        case MSG_SETUP_CANCEL:
            fDone = true;
            Lock();
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
//  App
// ============================================================

App::App()
    : BApplication("application/x-vnd.GoH-Tasks"),
      fAuth(nullptr), fAPI(nullptr), fMainWindow(nullptr)
{
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
        BAlert* alert = new BAlert("Setup Required",
                                   "No credentials configured. "
                                   "GoH-Tasks cannot run without OAuth credentials.",
                                   "OK", nullptr, nullptr,
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
                m.AddString("error", "Authentication failed. Please check credentials.");
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
