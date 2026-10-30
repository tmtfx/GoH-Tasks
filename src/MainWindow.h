#pragma once
#include <Window.h>
#include <ListView.h>
#include <Button.h>
#include <TextControl.h>
#include <TextView.h>
#include <StringView.h>
#include <MenuBar.h>
#include <ScrollView.h>
#include <vector>
#include "Task.h"
#include "GoogleAuth.h"
#include "GoogleTasksAPI.h"

// Message constants
const uint32 MSG_LIST_SELECTED    = 'lSel';
const uint32 MSG_TASK_SELECTED    = 'tSel';
const uint32 MSG_NEW_TASK         = 'nTsk';
const uint32 MSG_DELETE_TASK      = 'dTsk';
const uint32 MSG_TOGGLE_COMPLETE  = 'cTsk';
const uint32 MSG_TOGGLE_STAR      = 'sTsk';
const uint32 MSG_REFRESH          = 'rFsh';
const uint32 MSG_SEARCH_CHANGED   = 'srCh';
const uint32 MSG_SAVE_TASK        = 'savT';
const uint32 MSG_CANCEL_EDIT      = 'canE';
const uint32 MSG_TASKS_LOADED     = 'tLd!';
const uint32 MSG_LISTS_LOADED     = 'lLd!';
const uint32 MSG_ERROR            = 'eErr';
const uint32 MSG_NEW_TASKLIST     = 'nTLs';
const uint32 MSG_DELETE_TASKLIST  = 'dTLs';
const uint32 MSG_TASK_CREATED     = 'tCrt';
const uint32 MSG_TASK_UPDATED     = 'tUpd';
const uint32 MSG_TASK_DELETED     = 'tDel';
const uint32 MSG_OPEN_PREFS       = 'pref';

class MainWindow : public BWindow {
public:
    MainWindow(GoogleAuth* auth, GoogleTasksAPI* api);
    ~MainWindow() override;

    void MessageReceived(BMessage* msg) override;
    bool QuitRequested() override;

private:
    void BuildUI();
    void BuildMenuBar();
    void BuildToolbar(BView* parent, float& y);
    void BuildMainArea(BView* parent, float y);
    void BuildDetailPanel(BView* parent);

    void LoadTaskLists();
    void LoadTasks(const std::string& listId);
    void RefreshTaskDisplay();
    void ShowTaskDetail(const Task& task);
    void HideTaskDetail();
    void RevealDetailPanel();
    void ConcealDetailPanel();
    void UpdateToolbarState();
    void ShowError(const std::string& msg);
    void ShowStatus(const std::string& msg);
    void ApplySearch();

    static int32 LoadListsThread(void* data);
    static int32 LoadTasksThread(void* data);
    static int32 CreateTaskThread(void* data);
    static int32 UpdateTaskThread(void* data);
    static int32 DeleteTaskThread(void* data);

    GoogleAuth* fAuth;
    GoogleTasksAPI* fAPI;

    BListView*    fTaskListsView;
    BListView*    fTasksView;
    BButton*      fNewTaskBtn;
    BButton*      fDeleteBtn;
    BButton*      fCompleteBtn;
    BButton*      fStarBtn;
    BButton*      fRefreshBtn;
    BTextControl* fSearchField;
    BTextControl* fTitleField;
    BTextView*    fNotesView;
    BTextControl* fDueField;
    BButton*      fSaveBtn;
    BButton*      fCancelBtn;
    BStringView*  fStatusBar;
    BView*        fDetailPanel;
    BScrollView*  fListsScroll;
    BScrollView*  fTasksScroll;

    std::vector<TaskList> fTaskLists;
    std::vector<Task>     fTasks;
    std::vector<Task>     fFilteredTasks;
    std::string           fCurrentListId;
    int                   fSelectedTaskIndex = -1;
    bool                  fDetailVisible     = false;
    bool                  fIsNewTask         = false;
};
