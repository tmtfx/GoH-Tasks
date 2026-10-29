#pragma once
#include "Task.h"
#include <ListItem.h>
#include <View.h>

class TaskItem : public BListItem {
public:
    explicit TaskItem(const Task& task);

    void DrawItem(BView* owner, BRect frame, bool drawEverything) override;
    void Update(BView* owner, const BFont* font) override;

    const Task& GetTask() const { return fTask; }
    void SetTask(const Task& task) { fTask = task; }

private:
    Task fTask;
    float fCheckboxSize = 14.0f;
};
