#pragma once
#include <Application.h>
#include "GoogleAuth.h"
#include "GoogleTasksAPI.h"

class MainWindow;

class App : public BApplication {
public:
    App();
    ~App() override;
    void ReadyToRun() override;
    void MessageReceived(BMessage* msg) override;
    void AboutRequested() override;

private:
    void ShowSetupDialog();

    GoogleAuth*    fAuth;
    GoogleTasksAPI* fAPI;
    MainWindow*    fMainWindow;
};
