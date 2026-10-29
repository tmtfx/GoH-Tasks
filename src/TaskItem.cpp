#include "TaskItem.h"
#include <View.h>
#include <Font.h>
#include <String.h>
#include <cstring>
#include <cstdio>

TaskItem::TaskItem(const Task& task)
    : BListItem(), fTask(task)
{
}

void TaskItem::Update(BView* owner, const BFont* font)
{
    BListItem::Update(owner, font);
    // Ensure adequate row height
    font_height fh;
    font->GetHeight(&fh);
    float lineHeight = fh.ascent + fh.descent + fh.leading;
    if (Height() < lineHeight + 8.0f)
        SetHeight(lineHeight + 8.0f);
}

void TaskItem::DrawItem(BView* owner, BRect frame, bool drawEverything)
{
    // Background
    if (IsSelected()) {
        owner->SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
    } else {
        owner->SetHighColor(ui_color(B_LIST_BACKGROUND_COLOR));
    }
    owner->FillRect(frame);

    float margin  = 6.0f;
    float x       = frame.left + margin;
    float centerY = (frame.top + frame.bottom) / 2.0f;

    // --- Draw checkbox ---
    float cbSize = fCheckboxSize;
    BRect cbRect(x, centerY - cbSize / 2.0f, x + cbSize, centerY + cbSize / 2.0f);

    // Checkbox border
    owner->SetHighColor(100, 100, 100);
    owner->StrokeRect(cbRect);

    // Fill checkbox
    owner->SetHighColor(255, 255, 255);
    owner->FillRect(cbRect.InsetByCopy(1, 1));

    if (fTask.isCompleted()) {
        // Draw checkmark
        owner->SetHighColor(60, 160, 60);
        owner->SetPenSize(2.0f);
        BPoint p1(cbRect.left + 2, centerY);
        BPoint p2(cbRect.left + cbSize / 2.0f - 1, cbRect.bottom - 3);
        BPoint p3(cbRect.right - 2, cbRect.top + 3);
        owner->StrokeLine(p1, p2);
        owner->StrokeLine(p2, p3);
        owner->SetPenSize(1.0f);
    }
    x += cbSize + margin;

    // --- Draw star ---
    float starX = x;
    font_height fh;
    owner->GetFontHeight(&fh);
    float baseline = frame.top + (frame.Height() - (fh.ascent + fh.descent)) / 2.0f + fh.ascent;

    if (fTask.starred) {
        owner->SetHighColor(255, 180, 0);
        owner->DrawString("\xe2\x98\x85", BPoint(starX, baseline)); // UTF-8 ★
    } else {
        owner->SetHighColor(180, 180, 180);
        owner->DrawString("\xe2\x98\x86", BPoint(starX, baseline)); // UTF-8 ☆
    }

    // Approximate star width (use a fixed offset since star is a single glyph)
    float starWidth = owner->StringWidth("\xe2\x98\x85");
    x += starWidth + margin;

    // --- Draw due date at right edge (if set) ---
    std::string dueDisplay;
    if (!fTask.due.empty()) {
        // Extract date portion from RFC3339 (e.g. "2024-01-15T00:00:00.000Z" → "2024-01-15")
        dueDisplay = fTask.due.substr(0, 10);
    }
    float dueWidth = 0.0f;
    float rightEdge = frame.right - margin;
    if (!dueDisplay.empty()) {
        dueWidth = owner->StringWidth(dueDisplay.c_str());
        // Draw due date
        owner->SetHighColor(120, 120, 180);
        owner->DrawString(dueDisplay.c_str(), BPoint(rightEdge - dueWidth, baseline));
    }

    // --- Draw title text ---
    float titleMaxX = rightEdge - dueWidth - (dueWidth > 0 ? margin : 0);
    float titleWidth = titleMaxX - x;

    if (fTask.isCompleted()) {
        owner->SetHighColor(160, 160, 160);
    } else if (IsSelected()) {
        owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
    } else {
        owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
    }

    // Clip title if needed
    std::string title = fTask.title;
    float titleStrWidth = owner->StringWidth(title.c_str());
    if (titleStrWidth > titleWidth && titleWidth > 0) {
        // Truncate with ellipsis
        while (!title.empty() && owner->StringWidth((title + "…").c_str()) > titleWidth) {
            title.pop_back();
        }
        title += "…";
    }
    owner->DrawString(title.c_str(), BPoint(x, baseline));

    // --- Strikethrough for completed tasks ---
    if (fTask.isCompleted()) {
        float actualWidth = owner->StringWidth(title.c_str());
        float strikeY = baseline - fh.ascent / 2.5f;
        owner->SetHighColor(140, 140, 140);
        owner->StrokeLine(BPoint(x, strikeY), BPoint(x + actualWidth, strikeY));
    }
}
