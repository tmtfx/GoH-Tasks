#include "MainWindow.h"
#include "TaskItem.h"
#include "DatePickerWindow.h"

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
#include <Messenger.h>
#include <DateTime.h>
#include <TabView.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cctype>

static const float kDetailPanelHeight = 175.0f;

// ---- Due date helpers (RFC 3339 <-> BDate) ----

static bool ParseRfc3339Date(const std::string& s, int32& year, int32& month, int32& day)
{
    if (s.size() < 10)
        return false;
    year  = atoi(s.substr(0, 4).c_str());
    month = atoi(s.substr(5, 2).c_str());
    day   = atoi(s.substr(8, 2).c_str());
    return BPrivate::BDate::IsValid(year, month, day);
}

static std::string FormatRfc3339Date(int32 year, int32 month, int32 day)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT00:00:00.000Z", (int)year, (int)month, (int)day);
    return buf;
}

// ---- DueDateLabel: a fixed-width, clickable label showing the due date ----

class DueDateLabel : public BView {
public:
    DueDateLabel(BRect frame, const char* name)
        : BView(frame, name, B_FOLLOW_TOP, B_WILL_DRAW),
          fText("Due: (none)"), fHovering(false)
    {
        SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    }

    void SetText(const std::string& text)
    {
        fText = text;
        Invalidate();
    }

    void Draw(BRect updateRect) override
    {
        BRect bounds = Bounds();

        rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
        SetHighColor(fHovering ? tint_color(base, B_DARKEN_1_TINT) : base);
        FillRect(bounds);

        rgb_color border = ui_color(B_CONTROL_BORDER_COLOR);
        SetHighColor(border);
        StrokeRect(bounds);

        SetHighColor(ui_color(B_CONTROL_TEXT_COLOR));
        SetDrawingMode(B_OP_OVER);
        font_height fh;
        GetFontHeight(&fh);
        DrawString(fText.c_str(),
                   BPoint(6.0f, (bounds.Height() + fh.ascent - fh.descent) / 2.0f));
        SetDrawingMode(B_OP_COPY);
    }

    void MouseDown(BPoint where) override
    {
        BMessage msg(MSG_DUE_LABEL_CLICKED);
        Window()->PostMessage(&msg);
    }

    void MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage) override
    {
        bool wasHovering = fHovering;
        fHovering = (code == B_ENTERED_VIEW || code == B_INSIDE_VIEW);
        if (fHovering != wasHovering)
            Invalidate();
        BView::MouseMoved(where, code, dragMessage);
    }

private:
    std::string fText;
    bool        fHovering;
};

// ---- FilterTabView: a plain tab strip (no per-tab content) that posts
//      MSG_TASK_FILTER_CHANGED with the selected index when clicked ----

class FilterTabView : public BTabView {
public:
    FilterTabView(BRect frame, const char* name)
        : BTabView(frame, name, B_WIDTH_FROM_LABEL,
                   B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP)
    {
    }

    void Select(int32 index) override
    {
        BTabView::Select(index);
        BMessage msg(MSG_TASK_FILTER_CHANGED);
        msg.AddInt32("index", index);
        if (Window())
            Window()->PostMessage(&msg);
    }
};

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
      fNotesView(nullptr), fDueLabel(nullptr), fDueClearBtn(nullptr),
      fSaveBtn(nullptr), fCancelBtn(nullptr),
      fStatusBar(nullptr), fDetailPanel(nullptr),
      fListsScroll(nullptr), fTasksScroll(nullptr)
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
    // Probe the button's natural height for this system's font/theme instead
    // of guessing a pixel value — avoids label clipping on larger fonts.
    BButton* probe = new BButton(BRect(0, 0, 10, 10), "probe", "Mg", nullptr);
    float bh = 0.0f;
    probe->GetPreferredSize(nullptr, &bh);
    delete probe;
    if (bh < 24.0f) bh = 24.0f;

    float margin = 6.0f;
    float toolbarHeight = bh + margin * 2.0f;
    BView* toolbar = new BView(BRect(0, y, parent->Bounds().Width(), y + toolbarHeight),
                               "toolbar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP, B_WILL_DRAW);
    toolbar->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    parent->AddChild(toolbar);

    float bx = 6.0f;
    float by = margin;
    float bw = 90.0f;

    fNewTaskBtn = new RoundButton(BRect(bx, by, bx + bw, by + bh), "new_task",
                                  "New Task", new BMessage(MSG_NEW_TASK));
    toolbar->AddChild(fNewTaskBtn);
    bx += bw + 4;

    fCompleteBtn = new RoundButton(BRect(bx, by, bx + bw, by + bh), "complete",
                                   "Complete", new BMessage(MSG_TOGGLE_COMPLETE));
    toolbar->AddChild(fCompleteBtn);
    bx += bw + 4;

    fStarBtn = new RoundButton(BRect(bx, by, bx + 60, by + bh), "star",
                               "Star", new BMessage(MSG_TOGGLE_STAR));
    toolbar->AddChild(fStarBtn);
    bx += 64;

    fDeleteBtn = new RoundButton(BRect(bx, by, bx + bw, by + bh), "delete",
                                 "Delete", new BMessage(MSG_DELETE_TASK));
    toolbar->AddChild(fDeleteBtn);
    bx += bw + 4;

    fRefreshBtn = new RoundButton(BRect(bx, by, bx + bw, by + bh), "refresh",
                                  "Refresh", new BMessage(MSG_REFRESH));
    toolbar->AddChild(fRefreshBtn);
    bx += bw + 4;

    // Search field — aligned to the right area, vertically centered on the buttons
    float searchX = bx + 10.0f;
    float searchW = parent->Bounds().Width() - searchX - 10.0f;
    if (searchW < 100) searchW = 100;
    fSearchField = new BTextControl(BRect(searchX, by, searchX + searchW, by + bh),
                                    "search", "Search:", "",
                                    new BMessage(MSG_SEARCH_CHANGED));
    fSearchField->SetModificationMessage(new BMessage(MSG_SEARCH_CHANGED));
    toolbar->AddChild(fSearchField);

    float searchPrefH = 0.0f;
    fSearchField->GetPreferredSize(nullptr, &searchPrefH);
    if (searchPrefH > 0.0f) {
        float centerY = by + bh / 2.0f;
        fSearchField->ResizeTo(searchW, searchPrefH);
        fSearchField->MoveTo(searchX, centerY - searchPrefH / 2.0f);
    }

    y += toolbarHeight + 8;
}

void MainWindow::BuildMainArea(BView* parent, float y)
{
    float statusH  = 22.0f;
    // Lists start at full height — the detail panel is hidden by default and
    // only steals space from them when actually revealed (see RevealDetailPanel()).
    float mainH    = parent->Bounds().Height() - y - statusH - 2;
    float totalW   = parent->Bounds().Width();
    float listsW   = 180.0f;

    // --- Task lists panel (left) ---
    // Inset for the vertical scrollbar and the fancy border on both axes
    // (top+bottom, not just one) — using the full scroll-area size here
    // instead left no room for the scrollbar at all.
    fTaskListsView = new BListView(BRect(0, 0, listsW - B_V_SCROLL_BAR_WIDTH - 6, mainH - 4),
                                   "task_lists", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
    fTaskListsView->SetSelectionMessage(new BMessage(MSG_LIST_SELECTED));
    fListsScroll = new BScrollView("lists_scroll", fTaskListsView,
                                   B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM,
                                   0, false, true, B_FANCY_BORDER);
    fListsScroll->MoveTo(0, y);
    fListsScroll->ResizeTo(listsW, mainH);
    parent->AddChild(fListsScroll);

    // --- Tasks panel (right) ---
    float tasksX = listsW + 2;
    float tasksW = totalW - tasksX;

    // Filter tabs (All/Active/Completed/Starred) above the tasks list only.
    // Each tab's "content" is a throwaway empty view — the strip is just a
    // selector, it never shows per-tab content; see FilterTabView::Select().
    FilterTabView* filterTabs = new FilterTabView(
        BRect(tasksX, y, tasksX + tasksW, y + 30.0f), "task_filter_tabs");
    static const char* kFilterLabels[4] = { "All", "Active", "Completed", "Starred" };
    for (int i = 0; i < 4; ++i) {
        BView* empty = new BView(BRect(0, 0, 1, 1), "empty", B_FOLLOW_NONE, 0);
        BTab* tab = new BTab();
        filterTabs->AddTab(empty, tab);
        tab->SetLabel(kFilterLabels[i]);
    }
    float tabStripH = filterTabs->TabHeight();
    if (tabStripH < 20.0f) tabStripH = 20.0f;
    filterTabs->ResizeTo(tasksW, tabStripH + 4.0f);
    parent->AddChild(filterTabs);

    float tasksY = y + tabStripH + 4.0f + 4.0f;
    float tasksH = mainH - (tasksY - y);

    fTasksView = new BListView(BRect(0, 0, tasksW - B_V_SCROLL_BAR_WIDTH - 6, tasksH - 4),
                               "tasks", B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
    fTasksView->SetSelectionMessage(new BMessage(MSG_TASK_SELECTED));
    fTasksView->SetInvocationMessage(new BMessage(MSG_TASK_SELECTED));
    fTasksScroll = new BScrollView("tasks_scroll", fTasksView,
                                   B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP_BOTTOM,
                                   0, false, true, B_FANCY_BORDER);
    fTasksScroll->MoveTo(tasksX, tasksY);
    fTasksScroll->ResizeTo(tasksW, tasksH);
    parent->AddChild(fTasksScroll);

    // --- Detail panel (hidden until a task is selected/created; see
    //     RevealDetailPanel()/ConcealDetailPanel() for the show/hide + resize).
    //     Positioned flush below the full-height lists so it never overlaps
    //     them while hidden; RevealDetailPanel() moves it up when shown. ---
    float detailY = y + mainH;
    fDetailPanel = new BView(BRect(0, detailY, totalW, detailY + kDetailPanelHeight),
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
    float y  = 10.0f;
    float margin = 10.0f;
    float fieldH  = 24.0f;

    // Title — divider sized to the label itself, not the default ~50% split.
    fTitleField = new BTextControl(BRect(margin, y, pw - margin, y + fieldH),
                                   "title_field", "Title:", "",
                                   new BMessage((uint32)0));
    fTitleField->SetDivider(fTitleField->StringWidth("Title:") + 8.0f);
    fTitleField->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
    parent->AddChild(fTitleField);
    y += fieldH + 12.0f;

    // Notes — own label (BTextView has none built in) + a properly sized box.
    float notesRowY = y;
    float notesRowH = 76.0f;

    float notesLabelW = be_plain_font->StringWidth("Notes:") + 4.0f;
    BStringView* notesLabel = new BStringView(
        BRect(margin, notesRowY + 4, margin + notesLabelW, notesRowY + 20),
        "notes_label", "Notes:");
    notesLabel->SetResizingMode(B_FOLLOW_LEFT | B_FOLLOW_TOP);
    parent->AddChild(notesLabel);

    float notesX = margin + notesLabelW + 8.0f;
    float notesW = pw / 2.0f - notesX;
    BRect notesRect(0, 0, notesW, notesRowH);
    fNotesView = new BTextView(notesRect, "notes_view",
                               notesRect.InsetByCopy(4, 4),
                               B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
    fNotesView->SetWordWrap(true);
    BScrollView* notesScroll = new BScrollView("notes_scroll", fNotesView,
                                               B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
                                               0, false, true, B_FANCY_BORDER);
    notesScroll->MoveTo(notesX, notesRowY);
    notesScroll->ResizeTo(notesW, notesRowH);
    parent->AddChild(notesScroll);

    // Due date — a fixed-width clickable label (opens the date picker) plus
    // a small round "x" button to clear it. See ShowDatePicker()/UpdateDueLabel().
    float dueX = pw / 2.0f + margin;
    float dueLabelW = 160.0f;
    fDueLabel = new DueDateLabel(BRect(dueX, notesRowY, dueX + dueLabelW, notesRowY + fieldH),
                                 "due_label");
    fDueLabel->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
    parent->AddChild(fDueLabel);

    float clearX = dueX + dueLabelW + 6.0f;
    fDueClearBtn = new RoundButton(BRect(clearX, notesRowY, clearX + fieldH, notesRowY + fieldH),
                                   "due_clear", "x", new BMessage(MSG_DUE_CLEAR));
    fDueClearBtn->SetCornerRadius(fieldH / 2.0f);
    fDueClearBtn->SetResizingMode(B_FOLLOW_RIGHT | B_FOLLOW_TOP);
    parent->AddChild(fDueClearBtn);

    y = notesRowY + notesRowH + 14.0f;

    // Save / Cancel buttons
    float btnW = 80.0f;
    float btnH = 26.0f;
    fSaveBtn = new RoundButton(BRect(margin, y, margin + btnW, y + btnH),
                           "save_btn", "Save", new BMessage(MSG_SAVE_TASK));
    parent->AddChild(fSaveBtn);

    fCancelBtn = new RoundButton(BRect(margin + btnW + 8, y,
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
        bool matchesSearch = true;
        if (!search.empty()) {
            std::string title = t.title;
            std::transform(title.begin(), title.end(), title.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            matchesSearch = title.find(search) != std::string::npos;
        }

        bool matchesFilter = true;
        switch (fTaskFilter) {
        case 1: matchesFilter = !t.isCompleted(); break;  // Active
        case 2: matchesFilter = t.isCompleted();  break;  // Completed
        case 3: matchesFilter = t.starred;        break;  // Starred
        default: matchesFilter = true;            break;  // All
        }

        if (matchesSearch && matchesFilter)
            fFilteredTasks.push_back(t);
    }
}

void MainWindow::ShowTaskDetail(const Task& task)
{
    if (!fDetailPanel) return;
    fIsNewTask = false;
    fDetailVisible = true;

    if (fTitleField) fTitleField->SetText(task.title.c_str());
    if (fNotesView)  fNotesView->SetText(task.notes.c_str());
    fDueValue = task.due;
    UpdateDueLabel();

    RevealDetailPanel();
}

void MainWindow::HideTaskDetail()
{
    fDetailVisible = false;
    fIsNewTask = false;
    ConcealDetailPanel();
}

// Shows the detail panel, shrinking the lists above it to make room.
// Safe to call even if already revealed (Hide()/Show() use a nested
// counter, so calling either one more than once would leave it stuck).
void MainWindow::RevealDetailPanel()
{
    if (!fDetailPanel || !fDetailPanel->IsHidden())
        return;

    if (fListsScroll && fTasksScroll) {
        float shrinkBy = kDetailPanelHeight + 2.0f;
        fListsScroll->ResizeBy(0, -shrinkBy);
        fTasksScroll->ResizeBy(0, -shrinkBy);
        BRect listsFrame = fListsScroll->Frame();
        fDetailPanel->MoveTo(fDetailPanel->Frame().left, listsFrame.bottom + 2);
    }
    fDetailPanel->Show();
}

// Hides the detail panel, growing the lists back to fill the freed space.
void MainWindow::ConcealDetailPanel()
{
    if (!fDetailPanel || fDetailPanel->IsHidden())
        return;

    fDetailPanel->Hide();
    if (fListsScroll && fTasksScroll) {
        float growBy = kDetailPanelHeight + 2.0f;
        fListsScroll->ResizeBy(0, growBy);
        fTasksScroll->ResizeBy(0, growBy);
    }
}

void MainWindow::UpdateDueLabel()
{
    if (!fDueLabel) return;

    int32 year, month, day;
    if (!fDueValue.empty() && ParseRfc3339Date(fDueValue, year, month, day)) {
        BPrivate::BDate date(year, month, day);
        BString text;
        text << "Due: " << day << " " << date.ShortMonthName(month) << " " << year;
        fDueLabel->SetText(text.String());
    } else {
        fDueLabel->SetText("Due: (none)");
    }
}

void MainWindow::ShowDatePicker()
{
    if (!fDueLabel) return;

    int32 year, month, day;
    BPrivate::BDate initial; // invalid by default
    if (!fDueValue.empty() && ParseRfc3339Date(fDueValue, year, month, day))
        initial.SetDate(year, month, day);

    BPoint where = fDueLabel->ConvertToScreen(fDueLabel->Bounds().LeftBottom());
    where.y += 2.0f;

    DatePickerWindow* picker = new DatePickerWindow(
        where, BMessenger(this), new BMessage(MSG_DUE_DATE_PICKED), initial);
    picker->Show();
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
        fDueValue.clear();
        UpdateDueLabel();
        RevealDetailPanel();
        if (fTitleField) fTitleField->MakeFocus(true);
        break;
    }

    case MSG_SAVE_TASK: {
        if (fCurrentListId.empty()) break;
        const char* title = fTitleField ? fTitleField->Text() : "";
        const char* notes = fNotesView  ? fNotesView->Text()  : "";

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
            d->due    = fDueValue;
            resume_thread(spawn_thread(CreateTaskThread, "create_task",
                                       B_NORMAL_PRIORITY, d));
        } else {
            if (fSelectedTaskIndex < 0 ||
                fSelectedTaskIndex >= (int)fFilteredTasks.size()) break;
            Task task = fFilteredTasks[fSelectedTaskIndex];
            task.title = title;
            task.notes = notes ? notes : "";
            task.due   = fDueValue;
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
        fTasksView->DeselectAll();
        fSelectedTaskIndex = -1;
        UpdateToolbarState();
        break;
    }

    case MSG_DUE_LABEL_CLICKED: {
        ShowDatePicker();
        break;
    }

    case MSG_DUE_DATE_PICKED: {
        int32 year = 0, month = 0, day = 0;
        msg->FindInt32("year", &year);
        msg->FindInt32("month", &month);
        msg->FindInt32("day", &day);
        fDueValue = FormatRfc3339Date(year, month, day);
        UpdateDueLabel();
        break;
    }

    case MSG_DUE_CLEAR: {
        fDueValue.clear();
        UpdateDueLabel();
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

    case MSG_TASK_FILTER_CHANGED: {
        int32 index = 0;
        msg->FindInt32("index", &index);
        fTaskFilter = index;
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
