#include "DatePickerWindow.h"
#include "RoundButton.h"

#include <StringView.h>
#include <Screen.h>
#include <Font.h>
#include <Catalog.h>

#include <cstdio>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DatePickerWindow"

static const float kCellW      = 28.0f;
static const float kHeaderH    = 20.0f;
static const float kRowH       = 22.0f;
static const float kMargin     = 6.0f;
static const float kNavH       = 24.0f;
static const int   kRows       = 6;

static const uint32 kMsgPrevMonth = 'dpPm';
static const uint32 kMsgNextMonth = 'dpNm';

// ---- GridView: draws the day-number grid, handles click/double-click ----

class DatePickerWindow::GridView : public BView {
public:
    GridView(BRect frame, DatePickerWindow* owner)
        : BView(frame, "cal_grid", B_FOLLOW_NONE, B_WILL_DRAW),
          fOwner(owner)
    {
        SetViewUIColor(B_LIST_BACKGROUND_COLOR);
    }

    void Draw(BRect updateRect) override
    {
        BRect bounds = Bounds();

        // Day-of-week header
        SetHighColor(ui_color(B_LIST_BACKGROUND_COLOR));
        FillRect(BRect(bounds.left, bounds.top, bounds.right, kHeaderH - 1));
        SetHighColor(tint_color(ui_color(B_LIST_BACKGROUND_COLOR), B_DARKEN_1_TINT));
        static const char* kDayNames[7] = {
            B_TRANSLATE("Su"), B_TRANSLATE("Mo"), B_TRANSLATE("Tu"), B_TRANSLATE("We"),
            B_TRANSLATE("Th"), B_TRANSLATE("Fr"), B_TRANSLATE("Sa")
        };
        SetDrawingMode(B_OP_OVER);
        font_height fh;
        GetFontHeight(&fh);
        for (int col = 0; col < 7; ++col) {
            float x = col * kCellW;
            float textW = StringWidth(kDayNames[col]);
            DrawString(kDayNames[col],
                       BPoint(x + (kCellW - textW) / 2.0f,
                              kHeaderH - 4.0f));
        }

        const BDate& month = fOwner->fShownMonth;
        int32 daysInMonth = month.DaysInMonth();
        BDate firstOfMonth(month.Year(), month.Month(), 1);
        int32 startCol = firstOfMonth.DayOfWeek() % 7; // 0=Sunday per BDate? see note below

        int32 day = 1;
        for (int row = 0; row < kRows && day <= daysInMonth; ++row) {
            for (int col = 0; col < 7; ++col) {
                float x = col * kCellW;
                float y = kHeaderH + row * kRowH;
                BRect cell(x, y, x + kCellW - 1, y + kRowH - 1);

                if (row == 0 && col < startCol)
                    continue;
                if (day > daysInMonth)
                    continue;

                bool isHighlighted = fOwner->fHighlighted.IsValid()
                    && fOwner->fHighlighted.Year() == month.Year()
                    && fOwner->fHighlighted.Month() == month.Month()
                    && fOwner->fHighlighted.Day() == day;

                if (isHighlighted) {
                    SetHighColor(ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
                    FillRect(cell);
                    SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
                } else {
                    SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
                }

                char buf[4];
                snprintf(buf, sizeof(buf), "%d", (int)day);
                float textW = StringWidth(buf);
                DrawString(buf,
                           BPoint(x + (kCellW - textW) / 2.0f,
                                  y + (kRowH + fh.ascent - fh.descent) / 2.0f));
                ++day;
            }
        }
        SetDrawingMode(B_OP_COPY);
    }

    void MouseDown(BPoint where) override
    {
        if (where.y < kHeaderH)
            return;

        int32 col = (int32)(where.x / kCellW);
        int32 row = (int32)((where.y - kHeaderH) / kRowH);
        if (col < 0 || col > 6 || row < 0 || row >= kRows)
            return;

        const BDate& month = fOwner->fShownMonth;
        BDate firstOfMonth(month.Year(), month.Month(), 1);
        int32 startCol = firstOfMonth.DayOfWeek() % 7;
        int32 day = row * 7 + col - startCol + 1;
        if (day < 1 || day > month.DaysInMonth())
            return;

        int32 clicks = 1;
        BMessage* msg = Window()->CurrentMessage();
        if (msg)
            msg->FindInt32("clicks", &clicks);

        if (clicks >= 2) {
            fOwner->NotifyPick(month.Year(), month.Month(), day);
        } else {
            fOwner->fHighlighted.SetDate(month.Year(), month.Month(), day);
            Invalidate();
        }
    }

private:
    DatePickerWindow* fOwner;
};

// ---- DatePickerWindow ----

DatePickerWindow::DatePickerWindow(BPoint where, const BMessenger& target,
                                   BMessage* pickTemplate, const BDate& initialDate)
    : BWindow(BRect(0, 0, 10, 10), "", B_BORDERED_WINDOW,
              B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE
                  | B_NOT_MINIMIZABLE | B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE),
      fTarget(target), fPickTemplate(pickTemplate),
      fShownMonth(initialDate.IsValid() ? initialDate : BDate::CurrentDate(B_LOCAL_TIME)),
      fHighlighted(initialDate),
      fMonthLabel(nullptr), fGrid(nullptr), fSuppressFirstClose(true)
{
    SetFeel(B_FLOATING_ALL_WINDOW_FEEL);

    BuildUI();

    BRect frame = Frame();
    BRect screen = BScreen().Frame();
    if (where.x + frame.Width() > screen.right)
        where.x = screen.right - frame.Width();
    if (where.y + frame.Height() > screen.bottom)
        where.y -= (frame.Height() + 4.0f);
    MoveTo(where);
}

DatePickerWindow::~DatePickerWindow()
{
    delete fPickTemplate;
}

void DatePickerWindow::BuildUI()
{
    float gridW = kCellW * 7.0f;
    float gridH = kHeaderH + kRowH * kRows + 2.0f;
    float winW  = gridW + kMargin * 2.0f;
    float winH  = kMargin + kNavH + 6.0f + gridH + kMargin;
    ResizeTo(winW, winH);

    BView* bg = new BView(Bounds(), "bg", B_FOLLOW_ALL, B_WILL_DRAW);
    bg->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    AddChild(bg);

    RoundButton* prevBtn = new RoundButton(
        BRect(kMargin, kMargin, kMargin + kNavH, kMargin + kNavH),
        "prev", "<", new BMessage(kMsgPrevMonth));
    prevBtn->SetCornerRadius(kNavH / 2.0f);
    bg->AddChild(prevBtn);

    RoundButton* nextBtn = new RoundButton(
        BRect(winW - kMargin - kNavH, kMargin, winW - kMargin, kMargin + kNavH),
        "next", ">", new BMessage(kMsgNextMonth));
    nextBtn->SetCornerRadius(kNavH / 2.0f);
    bg->AddChild(nextBtn);

    fMonthLabel = new BStringView(
        BRect(kMargin + kNavH + 2, kMargin, winW - kMargin - kNavH - 2, kMargin + kNavH),
        "month_label", "");
    fMonthLabel->SetAlignment(B_ALIGN_CENTER);
    bg->AddChild(fMonthLabel);

    float gridY = kMargin + kNavH + 6.0f;
    fGrid = new GridView(BRect(kMargin, gridY, kMargin + gridW - 1, gridY + gridH - 1), this);
    bg->AddChild(fGrid);

    SetShownMonth(fShownMonth);
}

void DatePickerWindow::SetShownMonth(const BDate& date)
{
    fShownMonth = BDate(date.Year(), date.Month(), 1);

    BString text;
    text << fShownMonth.LongMonthName(fShownMonth.Month()) << " " << fShownMonth.Year();
    if (fMonthLabel)
        fMonthLabel->SetText(text.String());
    if (fGrid)
        fGrid->Invalidate();
}

void DatePickerWindow::NotifyPick(int32 year, int32 month, int32 day)
{
    BMessage msg(*fPickTemplate);
    msg.AddInt32("year", year);
    msg.AddInt32("month", month);
    msg.AddInt32("day", day);
    fTarget.SendMessage(&msg);
    PostMessage(B_QUIT_REQUESTED);
}

bool DatePickerWindow::QuitRequested()
{
    return true;
}

void DatePickerWindow::WindowActivated(bool active)
{
    if (active)
        return;
    if (fSuppressFirstClose) {
        fSuppressFirstClose = false;
        return;
    }
    PostMessage(B_QUIT_REQUESTED);
}

void DatePickerWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
    case kMsgPrevMonth: {
        BDate d = fShownMonth;
        d.AddMonths(-1);
        SetShownMonth(d);
        break;
    }
    case kMsgNextMonth: {
        BDate d = fShownMonth;
        d.AddMonths(1);
        SetShownMonth(d);
        break;
    }
    default:
        BWindow::MessageReceived(message);
        break;
    }
}
