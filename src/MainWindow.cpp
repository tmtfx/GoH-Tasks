#include "MainWindow.h"
#include "TaskItem.h"

#include <Application.h>
#include <Alert.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MenuBar.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Button.h>
#include <ListItem.h>
#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <SplitView.h>
#include <Box.h>
#include <Font.h>
#include <Screen.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>

// ---- Thread data structs ----

struct LoadListsData {
    BLooper*         looper;
    GoogleTasksAPI*  api;
};

struct LoadTasksData {
    BLooper*         looper;
    GoogleTasksAPI*  api;
    std::string      listId;
};

struct CreateTaskData {
    BLooper*         looper;
    GoogleTasksAPI*  api;
    std::string      listId;
    std::string      title;
    std::string      notes;
    std::string      due;
};

struct UpdateTaskData {
    BLooper*        looper;
    GoogleTasksAPI* api;
    std::string     listId;
    Task            task;
};

struct DeleteTaskData {
    BLooper*        looper;
    GoogleTasksAPI* api;
    std::string     listId;
    std::string     taskId;
};

// ---- MainWindow ----

MainWindow::MainWindow(GoogleAuth* auth, GoogleTasksAPI* api)
    : BWindow(BRect(100, 80, 900, 680), "GoH-Tasks",
              B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
      fAuth(auth), fAPI(api),
      fTaskListsView(nullptr), fTasksView(nullptr),
      fNewTaskBtn(nullptr), fDeleteBtn(nullptr),
      fCompleteBtn(nullptr), fStarBtn(nullptr), fRefreshBtn(nullptr),
      fSearchField(nullptr), fTitleField(nullptr),
      fNotesView(nullptr), fDueField(nullptr),
      fSaveBtn(nullptr), fCancelBtn(nullptr),
      fStatusBar(nullptr), fDetailPanel(nullptr)
{
    BuildUI();
    CenterOnScreen();
}

MainWindow::~MainWindow()
{
}

void MainWindow::BuildUI()
{
    BuildMenuBar();
    float menuBottom = KeyMenuBar()->Frame().bottom + 1;

    BRect rootFrame = Bounds();
    rootFrame.top = menuBottom;
    BView* root = new BView(rootFrame, "root", B_FOLLOW_ALL, B_WILL_DRAW);
    root->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    AddChild(root);

    float y = 0;
    BuildToolbar(root, y);
    BuildMainArea(root, y);
}

void MainWindow::BuildMenuBar()
{
    BMenuBar* menuBar = new BMenuBar(BRect(0, 0, Bounds().Width(), 20), "menu_bar");

    // File menu
    BMenu* fileMenu = new BMenu("File");
    fileMenu->AddItem(new BMenuItem("New Task",       new BMessage(MSG_NEW_TASK),    'N'));
    fileMenu->AddItem(new BMenuItem("New Task List",  new BMessage(MSG_NEW_TASKLIST)));
    fileMenu->AddSeparatorItem();
    fileMenu->AddItem(new BMenuItem("Preferences…",  new BMessage(MSG_OPEN_PREFS)));
    fileMenu->AddItem(new BMenuItem("About GoH-Tasks…", new BMessage(B_ABOUT_REQUESTED)));
    fileMenu->AddSeparatorItem();
    fileMenu->AddItem(new BMenuItem("Quit",           new BMessage(B_QUIT_REQUESTED), 'Q'));
    menuBar->AddItem(fileMenu);

    // Tasks menu
    BMenu* tasksMenu = new BMenu("Tasks");
    tasksMenu->AddItem(new BMenuItem("Refresh",       new BMessage(MSG_REFRESH),      'R'));
    tasksMenu->AddSeparatorItem();
    tasksMenu->AddItem(new BMenuItem("Toggle Complete", new BMessage(MSG_TOGGLE_COMPLETE)));
    tasksMenu->AddItem(new BMenuItem("Toggle Star",     new BMessage(MSG_TOGGLE_STAR)));
    tasksMenu->AddSeparatorItem();
    tasksMenu->AddItem(new BMenuItem("Delete Task",   new BMessage(MSG_DELETE_TASK)));
    menuBar->AddItem(tasksMenu);

    AddChild(menuBar);
}

void MainWindow::BuildToolbar(BView* parent, float& y)
{
    float toolbarHeight = 36.0f;
    BView* toolbar = new BView(BRect(0, y, parent->Bounds().Width(), y + toolbarHeight),
                               "toolbar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW);
    toolbar->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    parent->AddChild(toolbar);

    float bx = 6.0f;
    float by = 4.0f;
    float bw = 90.0f;
    float bh = 26.0f;

    fNewTaskBtn = new BButton(BRect(bx, by, bx + bw, by + bh), "new_task",
                              "New Task", new BMessage(MSG_NEW_TASK));
    toolbar->AddChild(fNewTaskBtn);
    bx += bw + 4;

    fCompleteBtn = new BButton(BRect(bx, by, bx + bw, by + bh), "complete",
                               "Complete", new BMessage(MSG_TOGGLE_COMPLETE));
    toolbar->AddChild(fCompleteBtn);
    bx += bw + 4;

    fStarBtn = new BButton(BRect(bx, by, bx + 60, by + bh), "star",
                           "Star", new BMessage(MSG_TOGGLE_STAR));
    toolbar->AddChild(fStarBtn);
    bx += 64;

    fDeleteBtn = new BButton(BRect(bx, by, bx + bw, by + bh), "delete",
                             "Delete", new BMessage(MSG_DELETE_TASK));
    toolbar->AddChild(fDeleteBtn);
    bx += bw + 4;

    fRefreshBtn = new BButton(BRect(bx, by, bx + bw, by + bh), "refresh",
                              "Refresh", new BMessage(MSG_REFRESH));
    toolbar->AddChild(fRefreshBtn);
    bx += bw + 4;

    // Search field — aligned to the right area
    float searchX = bx + 10.0f;
    float searchW = parent->Bounds().Width() - searchX - 10.0f;
    if (searchW < 100) searchW = 100;
    fSearchField = new BTextControl(BRect(searchX, by - 2, searchX + searchW, by + bh + 2),
                                    "search", "Search:", "",
                                    new BMessage(MSG_SEARCH_CHANGED));
    fSearchField->SetModificationMessage(new BMessage(MSG_SEARCH_CHANGED));
    toolbar->AddChild(fSearchField);

    y += toolbarHeight + 2;
}

void MainWindow::BuildMainArea(BView* parent, float y)
{
    float statusH  = 22.0f;
    float detailH  = 130.0f;
    float mainH    = parent->Bounds().Height() - y - statusH - detailH - 4;
    float totalW   = parent->Bounds().Width();
    float listsW   = 180.0f;

    // --- Task lists panel (left) ---
    fTaskListsView = new BListView(BRect(0, 0, listsW - B_V_SCROLL_BAR_WIDTH - 2, mainH - 2),
                                   "task_lists", B_SINGLE_SELECTION_LIST);
    fTaskListsView->SetSelectionMessage(new BMessage(MSG_LIST_SELECTED));
    BScrollView* listsScroll = new BScrollView("lists_scroll", fTaskListsView,
                                               B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM,
                                               0, false, true, B_FANCY_BORDER);
    listsScroll->MoveTo(0, y);
    listsScroll->ResizeTo(listsW, mainH);
    parent->AddChild(listsScroll);

    // --- Tasks panel (right) ---
    float tasksX = listsW + 2;
    float tasksW = totalW - tasksX;
    fTasksView = new BListView(BRect(0, 0, tasksW - B_V_SCROLL_BAR_WIDTH - 2, mainH - 2),
                               "tasks", B_SINGLE_SELECTION_LIST);
    fTasksView->SetSelectionMessage(new BMessage(MSG_TASK_SELECTED));
    fTasksView->SetInvocationMessage(new BMessage(MSG_TASK_SELECTED));
    BScrollView* tasksScroll = new BScrollView("tasks_scroll", fTasksView,
                                               B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP_BOTTOM,
                                               0, false, true, B_FANCY_BORDER);
    tasksScroll->MoveTo(tasksX, y);
    tasksScroll->ResizeTo(tasksW, mainH);
    parent->AddChild(tasksScroll);

    float detailY = y + mainH + 2;

    // --- Detail panel ---
    fDetailPanel = new BView(BRect(0, detailY, totalW, detailY + detailH),
                             "detail_panel", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM,
                             B_WILL_DRAW);
    fDetailPanel->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    parent->AddChild(fDetailPanel);
    BuildDetailPanel(fDetailPanel);
    fDetailPanel->Hide();

    // --- Status bar ---
    float sbY = parent->Bounds().Height() - statusH;
    fStatusBar = new BStringView(BRect(4, sbY, totalW - 4, sbY + statusH - 2),
                                 "status", "Ready");
    fStatusBar->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);
    parent->AddChild(fStatusBar);

    UpdateToolbarState();
}

void MainWindow::BuildDetailPanel(BView* parent)
{
    float pw = parent->Bounds().Width();
    float y  = 4.0f;
    float margin = 8.0f;

    // Title
    fTitleField = new BTextControl(BRect(margin, y, pw - margin, y + 22),
                                   "title_field", "Title:", "",
                                   new BMessage((uint32)0));
    fTitleField->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
    parent->AddChild(fTitleField);
    y += 26;

    // Notes
    BRect notesRect(margin + 40, y, pw / 2.0f, y + 40);
    fNotesView = new BTextView(notesRect, "notes_view",
                               notesRect.OffsetToCopy(0, 0),
                               B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
    fNotesView->SetWordWrap(true);
    BScrollView* notesScroll = new BScrollView("notes_scroll", fNotesView,
                                               B_FOLLOW_LEFT | B_FOLLOW_TOP,
                                               0, false, true, B_FANCY_BORDER);
    notesScroll->MoveTo(margin, y);
    notesScroll->ResizeTo(pw / 2.0f - margin * 2, 44);
    parent->AddChild(notesScroll);

    // Due date
    float dueX = pw / 2.0f + margin;
    fDueField = new BTextControl(BRect(dueX, y, pw - margin, y + 22),
                                 "due_field", "Due:", "", new BMessage((uint32)0));
    fDueField->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
    parent->AddChild(fDueField);
    y += 50;

    // Save / Cancel buttons
    float btnW = 80.0f;
    float btnH = 24.0f;
    fSaveBtn = new BButton(BRect(margin, y, margin + btnW, y + btnH),
                           "save_btn", "Save", new BMessage(MSG_SAVE_TASK));
    fSaveBtn->MakeDefault(true);
    parent->AddChild(fSaveBtn);

    fCancelBtn = new BButton(BRect(margin + btnW + 8, y,
                                   margin + btnW * 2 + 8, y + btnH),
                             "cancel_btn", "Cancel", new BMessage(MSG_CANCEL_EDIT));
    parent->AddChild(fCancelBtn);
}

// ---- Thread functions ----

int32 MainWindow::LoadListsThread(void* data)
{
    LoadListsData* d = static_cast<LoadListsData*>(data);
    std::vector<TaskList> lists;
    bool ok = d->api->getTaskLists(lists);

    BMessage msg(ok ? MSG_LISTS_LOADED : MSG_ERROR);
    if (ok) {
        for (size_t i = 0; i < lists.size(); ++i) {
            std::string key = "id_"    + std::to_string(i);
            std::string tkey= "title_" + std::to_string(i);
            msg.AddString(key.c_str(),   lists[i].id.c_str());
            msg.AddString(tkey.c_str(),  lists[i].title.c_str());
        }
        msg.AddInt32("count", (int32)lists.size());
    } else {
        msg.AddString("error", d->api->lastError().c_str());
    }
    d->looper->PostMessage(&msg);
    delete d;
    return 0;
}

int32 MainWindow::LoadTasksThread(void* data)
{
    LoadTasksData* d = static_cast<LoadTasksData*>(data);
    std::vector<Task> tasks;
    bool ok = d->api->getTasks(d->listId, tasks);

    BMessage msg(ok ? MSG_TASKS_LOADED : MSG_ERROR);
    if (ok) {
        for (size_t i = 0; i < tasks.size(); ++i) {
            std::string prefix = std::to_string(i) + "_";
            msg.AddString((prefix + "id").c_str(),       tasks[i].id.c_str());
            msg.AddString((prefix + "title").c_str(),    tasks[i].title.c_str());
            msg.AddString((prefix + "notes").c_str(),    tasks[i].notes.c_str());
            msg.AddString((prefix + "status").c_str(),   tasks[i].status.c_str());
            msg.AddString((prefix + "due").c_str(),      tasks[i].due.c_str());
            msg.AddString((prefix + "updated").c_str(),  tasks[i].updated.c_str());
            msg.AddString((prefix + "selfLink").c_str(), tasks[i].selfLink.c_str());
            msg.AddBool((prefix  + "starred").c_str(),   tasks[i].starred);
        }
        msg.AddInt32("count", (int32)tasks.size());
        msg.AddString("listId", d->listId.c_str());
    } else {
        msg.AddString("error", d->api->lastError().c_str());
    }
    d->looper->PostMessage(&msg);
    delete d;
    return 0;
}

int32 MainWindow::CreateTaskThread(void* data)
{
    CreateTaskData* d = static_cast<CreateTaskData*>(data);
    Task created;
    bool ok = d->api->createTask(d->listId, d->title, d->notes, d->due, created);

    BMessage msg(ok ? MSG_TASK_CREATED : MSG_ERROR);
    if (ok) {
        msg.AddString("listId",   d->listId.c_str());
        msg.AddString("id",       created.id.c_str());
        msg.AddString("title",    created.title.c_str());
        msg.AddString("notes",    created.notes.c_str());
        msg.AddString("status",   created.status.c_str());
        msg.AddString("due",      created.due.c_str());
        msg.AddString("updated",  created.updated.c_str());
        msg.AddString("selfLink", created.selfLink.c_str());
    } else {
        msg.AddString("error", d->api->lastError().c_str());
    }
    d->looper->PostMessage(&msg);
    delete d;
    return 0;
}

int32 MainWindow::UpdateTaskThread(void* data)
{
    UpdateTaskData* d = static_cast<UpdateTaskData*>(data);
    bool ok = d->api->updateTask(d->listId, d->task);

    BMessage msg(ok ? MSG_TASK_UPDATED : MSG_ERROR);
    if (!ok) msg.AddString("error", d->api->lastError().c_str());
    msg.AddString("listId", d->listId.c_str());
    d->looper->PostMessage(&msg);
    delete d;
    return 0;
}

int32 MainWindow::DeleteTaskThread(void* data)
{
    DeleteTaskData* d = static_cast<DeleteTaskData*>(data);
    bool ok = d->api->deleteTask(d->listId, d->taskId);

    BMessage msg(ok ? MSG_TASK_DELETED : MSG_ERROR);
    if (!ok) msg.AddString("error", d->api->lastError().c_str());
    msg.AddString("listId", d->listId.c_str());
    msg.AddString("taskId", d->taskId.c_str());
    d->looper->PostMessage(&msg);
    delete d;
    return 0;
}

// ---- Public methods ----

void MainWindow::LoadTaskLists()
{
    ShowStatus("Loading task lists…");
    LoadListsData* d = new LoadListsData;
    d->looper = this;
    d->api    = fAPI;
    resume_thread(spawn_thread(LoadListsThread, "load_lists", B_NORMAL_PRIORITY, d));
}

void MainWindow::LoadTasks(const std::string& listId)
{
    fCurrentListId = listId;
    ShowStatus("Loading tasks…");
    LoadTasksData* d = new LoadTasksData;
    d->looper = this;
    d->api    = fAPI;
    d->listId = listId;
    resume_thread(spawn_thread(LoadTasksThread, "load_tasks", B_NORMAL_PRIORITY, d));
}

void MainWindow::RefreshTaskDisplay()
{
    // Apply current search filter
    ApplySearch();

    // Rebuild list view items
    fTasksView->MakeEmpty();
    for (const Task& t : fFilteredTasks) {
        fTasksView->AddItem(new TaskItem(t));
    }

    // Update status
    int total    = (int)fTasks.size();
    int completed = 0;
    for (const Task& t : fTasks)
        if (t.isCompleted()) ++completed;
    char buf[128];
    snprintf(buf, sizeof(buf), "%d task%s • %d completed",
             total, total == 1 ? "" : "s", completed);
    ShowStatus(buf);
    UpdateToolbarState();
}

void MainWindow::ApplySearch()
{
    const char* searchText = fSearchField ? fSearchField->Text() : "";
    std::string search(searchText);
    // lowercase
    std::transform(search.begin(), search.end(), search.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    fFilteredTasks.clear();
    for (const Task& t : fTasks) {
        if (search.empty()) {
            fFilteredTasks.push_back(t);
        } else {
            std::string title = t.title;
            std::transform(title.begin(), title.end(), title.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (title.find(search) != std::string::npos)
                fFilteredTasks.push_back(t);
        }
    }
}

void MainWindow::ShowTaskDetail(const Task& task)
{
    if (!fDetailPanel) return;
    fIsNewTask = false;
    fDetailVisible = true;

    if (fTitleField) fTitleField->SetText(task.title.c_str());
    if (fNotesView)  fNotesView->SetText(task.notes.c_str());
    if (fDueField)   fDueField->SetText(task.due.c_str());

    fDetailPanel->Show();
}

void MainWindow::HideTaskDetail()
{
    fDetailVisible = false;
    fIsNewTask = false;
    if (fDetailPanel) fDetailPanel->Hide();
}

void MainWindow::UpdateToolbarState()
{
    bool hasSelection = (fSelectedTaskIndex >= 0 &&
                         fSelectedTaskIndex < (int)fFilteredTasks.size());
    if (fDeleteBtn)   fDeleteBtn->SetEnabled(hasSelection);
    if (fCompleteBtn) fCompleteBtn->SetEnabled(hasSelection);
    if (fStarBtn)     fStarBtn->SetEnabled(hasSelection);
    if (fNewTaskBtn)  fNewTaskBtn->SetEnabled(!fCurrentListId.empty());
}

void MainWindow::ShowError(const std::string& msg)
{
    ShowStatus(std::string("Error: ") + msg);
    // Also show alert for serious errors
    BAlert* alert = new BAlert("Error", msg.c_str(), "OK",
                               nullptr, nullptr,
                               B_WIDTH_AS_USUAL, B_WARNING_ALERT);
    alert->SetShortcut(0, B_ESCAPE);
    alert->Go();
}

void MainWindow::ShowStatus(const std::string& msg)
{
    if (fStatusBar) fStatusBar->SetText(msg.c_str());
}

// ---- MessageReceived ----

void MainWindow::MessageReceived(BMessage* msg)
{
    switch (msg->what) {

    case MSG_LISTS_LOADED: {
        fTaskLists.clear();
        int32 count = 0;
        msg->FindInt32("count", &count);
        for (int32 i = 0; i < count; ++i) {
            TaskList tl;
            const char* id    = nullptr;
            const char* title = nullptr;
            std::string key   = "id_"    + std::to_string(i);
            std::string tkey  = "title_" + std::to_string(i);
            msg->FindString(key.c_str(),  &id);
            msg->FindString(tkey.c_str(), &title);
            if (id)    tl.id    = id;
            if (title) tl.title = title;
            fTaskLists.push_back(tl);
        }
        fTaskListsView->MakeEmpty();
        for (const TaskList& tl : fTaskLists) {
            fTaskListsView->AddItem(new BStringItem(tl.title.c_str()));
        }
        if (!fTaskLists.empty()) {
            fTaskListsView->Select(0);
            LoadTasks(fTaskLists[0].id);
        } else {
            ShowStatus("No task lists found. Create one first.");
        }
        break;
    }

    case MSG_TASKS_LOADED: {
        fTasks.clear();
        int32 count = 0;
        msg->FindInt32("count", &count);
        for (int32 i = 0; i < count; ++i) {
            Task t;
            std::string prefix = std::to_string(i) + "_";
            const char* val = nullptr;
            if (msg->FindString((prefix + "id").c_str(),       &val) == B_OK) t.id       = val;
            if (msg->FindString((prefix + "title").c_str(),    &val) == B_OK) t.title    = val;
            if (msg->FindString((prefix + "notes").c_str(),    &val) == B_OK) t.notes    = val;
            if (msg->FindString((prefix + "status").c_str(),   &val) == B_OK) t.status   = val;
            if (msg->FindString((prefix + "due").c_str(),      &val) == B_OK) t.due      = val;
            if (msg->FindString((prefix + "updated").c_str(),  &val) == B_OK) t.updated  = val;
            if (msg->FindString((prefix + "selfLink").c_str(), &val) == B_OK) t.selfLink = val;
            bool starred = false;
            msg->FindBool((prefix + "starred").c_str(), &starred);
            t.starred = starred;
            if (t.status.empty()) t.status = "needsAction";
            fTasks.push_back(t);
        }
        RefreshTaskDisplay();
        break;
    }

    case MSG_TASK_CREATED: {
        const char* listId = nullptr;
        msg->FindString("listId", &listId);
        if (listId && fCurrentListId == listId) {
            HideTaskDetail();
            LoadTasks(fCurrentListId);
        }
        break;
    }

    case MSG_TASK_UPDATED: {
        HideTaskDetail();
        LoadTasks(fCurrentListId);
        break;
    }

    case MSG_TASK_DELETED: {
        fSelectedTaskIndex = -1;
        HideTaskDetail();
        LoadTasks(fCurrentListId);
        break;
    }

    case MSG_ERROR: {
        const char* errStr = nullptr;
        msg->FindString("error", &errStr);
        ShowStatus(std::string("Error: ") + (errStr ? errStr : "Unknown error"));
        break;
    }

    case MSG_LIST_SELECTED: {
        int32 index = fTaskListsView->CurrentSelection();
        if (index >= 0 && index < (int32)fTaskLists.size()) {
            HideTaskDetail();
            fSelectedTaskIndex = -1;
            LoadTasks(fTaskLists[index].id);
        }
        break;
    }

    case MSG_TASK_SELECTED: {
        int32 index = fTasksView->CurrentSelection();
        fSelectedTaskIndex = (int)index;
        UpdateToolbarState();
        if (index >= 0 && index < (int32)fFilteredTasks.size()) {
            ShowTaskDetail(fFilteredTasks[index]);
        } else {
            HideTaskDetail();
        }
        break;
    }

    case MSG_NEW_TASK: {
        if (fCurrentListId.empty()) {
            ShowStatus("Please select a task list first.");
            break;
        }
        fIsNewTask = true;
        fDetailVisible = true;
        if (fTitleField) fTitleField->SetText("");
        if (fNotesView)  fNotesView->SetText("");
        if (fDueField)   fDueField->SetText("");
        if (fDetailPanel) fDetailPanel->Show();
        if (fTitleField) fTitleField->MakeFocus(true);
        break;
    }

    case MSG_SAVE_TASK: {
        if (fCurrentListId.empty()) break;
        const char* title = fTitleField ? fTitleField->Text() : "";
        const char* notes = fNotesView  ? fNotesView->Text()  : "";
        const char* due   = fDueField   ? fDueField->Text()   : "";

        if (!title || strlen(title) == 0) {
            ShowStatus("Task title cannot be empty.");
            break;
        }

        if (fIsNewTask) {
            ShowStatus("Creating task…");
            CreateTaskData* d = new CreateTaskData;
            d->looper = this;
            d->api    = fAPI;
            d->listId = fCurrentListId;
            d->title  = title;
            d->notes  = notes ? notes : "";
            d->due    = due   ? due   : "";
            resume_thread(spawn_thread(CreateTaskThread, "create_task",
                                       B_NORMAL_PRIORITY, d));
        } else {
            if (fSelectedTaskIndex < 0 ||
                fSelectedTaskIndex >= (int)fFilteredTasks.size()) break;
            Task task = fFilteredTasks[fSelectedTaskIndex];
            task.title = title;
            task.notes = notes ? notes : "";
            task.due   = due   ? due   : "";
            ShowStatus("Updating task…");
            UpdateTaskData* d = new UpdateTaskData;
            d->looper = this;
            d->api    = fAPI;
            d->listId = fCurrentListId;
            d->task   = task;
            resume_thread(spawn_thread(UpdateTaskThread, "update_task",
                                       B_NORMAL_PRIORITY, d));
        }
        break;
    }

    case MSG_CANCEL_EDIT: {
        HideTaskDetail();
        break;
    }

    case MSG_DELETE_TASK: {
        if (fSelectedTaskIndex < 0 ||
            fSelectedTaskIndex >= (int)fFilteredTasks.size()) break;
        const Task& task = fFilteredTasks[fSelectedTaskIndex];

        BAlert* alert = new BAlert("Delete Task",
                                   ("Delete task \"" + task.title + "\"?").c_str(),
                                   "Cancel", "Delete", nullptr,
                                   B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        int32 result = alert->Go();
        if (result == 1) {
            ShowStatus("Deleting task…");
            DeleteTaskData* d = new DeleteTaskData;
            d->looper = this;
            d->api    = fAPI;
            d->listId = fCurrentListId;
            d->taskId = task.id;
            resume_thread(spawn_thread(DeleteTaskThread, "delete_task",
                                       B_NORMAL_PRIORITY, d));
        }
        break;
    }

    case MSG_TOGGLE_COMPLETE: {
        if (fSelectedTaskIndex < 0 ||
            fSelectedTaskIndex >= (int)fFilteredTasks.size()) break;
        Task& task = fFilteredTasks[fSelectedTaskIndex];
        bool newState = !task.isCompleted();
        ShowStatus(newState ? "Marking complete…" : "Marking incomplete…");

        // Update optimistically in UI
        task.status = newState ? "completed" : "needsAction";
        // Find and update in fTasks too
        for (Task& t : fTasks) {
            if (t.id == task.id) {
                t.status = task.status;
                break;
            }
        }
        // Refresh the list item
        TaskItem* item = dynamic_cast<TaskItem*>(fTasksView->ItemAt(fSelectedTaskIndex));
        if (item) {
            item->SetTask(task);
            fTasksView->InvalidateItem(fSelectedTaskIndex);
        }

        // Fire API call in background
        struct CompleteData {
            BLooper*        looper;
            GoogleTasksAPI* api;
            std::string     listId;
            std::string     taskId;
            bool            complete;
        };
        CompleteData* cd = new CompleteData;
        cd->looper   = this;
        cd->api      = fAPI;
        cd->listId   = fCurrentListId;
        cd->taskId   = task.id;
        cd->complete = newState;

        resume_thread(spawn_thread([](void* data) -> int32 {
            CompleteData* cd = static_cast<CompleteData*>(data);
            bool ok = cd->api->completeTask(cd->listId, cd->taskId, cd->complete);
            BMessage m(ok ? MSG_TASK_UPDATED : MSG_ERROR);
            if (!ok) m.AddString("error", cd->api->lastError().c_str());
            m.AddString("listId", cd->listId.c_str());
            cd->looper->PostMessage(&m);
            delete cd;
            return 0;
        }, "toggle_complete", B_NORMAL_PRIORITY, cd));
        break;
    }

    case MSG_TOGGLE_STAR: {
        if (fSelectedTaskIndex < 0 ||
            fSelectedTaskIndex >= (int)fFilteredTasks.size()) break;
        Task& task = fFilteredTasks[fSelectedTaskIndex];
        task.starred = !task.starred;
        // Sync back to fTasks
        for (Task& t : fTasks) {
            if (t.id == task.id) {
                t.starred = task.starred;
                break;
        }
        }
        // Save starred state
        struct StarData {
            GoogleTasksAPI* api;
            std::string     taskId;
            bool            starred;
        };
        StarData* sd = new StarData;
        sd->api     = fAPI;
        sd->taskId  = task.id;
        sd->starred = task.starred;
        resume_thread(spawn_thread([](void* data) -> int32 {
            StarData* sd = static_cast<StarData*>(data);
            sd->api->setTaskStarred(sd->taskId, sd->starred);
            delete sd;
            return 0;
        }, "toggle_star", B_NORMAL_PRIORITY, sd));

        // Refresh the row
        TaskItem* item = dynamic_cast<TaskItem*>(fTasksView->ItemAt(fSelectedTaskIndex));
        if (item) {
            item->SetTask(task);
            fTasksView->InvalidateItem(fSelectedTaskIndex);
        }
        break;
    }

    case MSG_REFRESH: {
        if (!fCurrentListId.empty())
            LoadTasks(fCurrentListId);
        else
            LoadTaskLists();
        break;
    }

    case MSG_SEARCH_CHANGED: {
        RefreshTaskDisplay();
        break;
    }

    case MSG_NEW_TASKLIST: {
        BAlert* alert = new BAlert("New Task List", "Enter a name for the new task list:",
                                   "Cancel", "Create", nullptr,
                                   B_WIDTH_AS_USUAL, B_INFO_ALERT);
        // Unfortunately BAlert doesn't have a text field — use a simple approach:
        // Show a dialog via a window
        // For simplicity, use a BAlert with a hardcoded "New List" name and let
        // the user rename it, since Haiku's BAlert doesn't support text input.
        // A proper implementation would use a custom BWindow.
        alert->SetShortcut(0, B_ESCAPE);
        int32 result = alert->Go();
        if (result == 1) {
            TaskList created;
            if (fAPI->createTaskList("New List", created)) {
                LoadTaskLists();
            } else {
                ShowError(fAPI->lastError());
            }
        }
        break;
    }

    case MSG_DELETE_TASKLIST: {
        int32 index = fTaskListsView->CurrentSelection();
        if (index < 0 || index >= (int32)fTaskLists.size()) break;
        const TaskList& tl = fTaskLists[index];
        BAlert* alert = new BAlert("Delete List",
                                   ("Delete list \"" + tl.title + "\" and all its tasks?").c_str(),
                                   "Cancel", "Delete", nullptr,
                                   B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        alert->SetShortcut(0, B_ESCAPE);
        int32 result = alert->Go();
        if (result == 1) {
            if (fAPI->deleteTaskList(tl.id)) {
                fCurrentListId.clear();
                fTasks.clear();
                fFilteredTasks.clear();
                fTasksView->MakeEmpty();
                LoadTaskLists();
            } else {
                ShowError(fAPI->lastError());
            }
        }
        break;
    }

    case MSG_OPEN_PREFS: {
        // Delegate back to app to show setup dialog
        be_app->PostMessage(MSG_OPEN_PREFS);
        break;
    }

    case B_ABOUT_REQUESTED: {
        be_app->PostMessage(B_ABOUT_REQUESTED);
        break;
    }

    default:
        BWindow::MessageReceived(msg);
        break;
    }
}

bool MainWindow::QuitRequested()
{
    be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}
