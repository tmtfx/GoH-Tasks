#pragma once
#include <Window.h>
#include <Messenger.h>
#include <DateTime.h>

class BStringView;
class RoundButton;

using BPrivate::BDate;

// A small floating popup showing a month grid of days. Single click
// highlights a day, double click picks it (sends pickTemplate, with
// "year"/"month"/"day" int32 fields added, to target) and closes.
class DatePickerWindow : public BWindow {
public:
    // Takes ownership of pickTemplate. initialDate selects the month shown
    // and the initially-highlighted day; pass an invalid BDate for "none".
    DatePickerWindow(BPoint where, const BMessenger& target,
                     BMessage* pickTemplate, const BDate& initialDate);
    ~DatePickerWindow() override;

    bool QuitRequested() override;
    void WindowActivated(bool active) override;
    void MessageReceived(BMessage* message) override;

private:
    class GridView;
    friend class GridView;

    void BuildUI();
    void SetShownMonth(const BDate& date);
    void NotifyPick(int32 year, int32 month, int32 day);

    BMessenger   fTarget;
    BMessage*    fPickTemplate;
    BDate        fShownMonth;
    BDate        fHighlighted;
    BStringView* fMonthLabel;
    GridView*    fGrid;
    bool         fSuppressFirstClose;
};
