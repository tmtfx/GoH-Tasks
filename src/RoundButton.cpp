#include "RoundButton.h"

#include <Window.h>
#include <Message.h>
#include <Font.h>

RoundButton::RoundButton(BRect frame, const char* name, const char* label, BMessage* message,
                         uint32 resizingMode, uint32 flags)
    : BControl(frame, name, label, message, resizingMode, flags),
      fHovering(false), fTracking(false), fCornerRadius(8.0f)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
}

RoundButton::~RoundButton()
{
}

void RoundButton::Draw(BRect updateRect)
{
    BRect bounds = Bounds();

    rgb_color base   = ui_color(B_CONTROL_BACKGROUND_COLOR);
    rgb_color border = ui_color(B_CONTROL_BORDER_COLOR);
    rgb_color text   = ui_color(B_CONTROL_TEXT_COLOR);

    rgb_color fill = base;
    if (!IsEnabled())
        fill = tint_color(base, B_LIGHTEN_1_TINT);
    else if (fTracking)
        fill = tint_color(base, B_DARKEN_2_TINT);
    else if (fHovering)
        fill = tint_color(base, B_DARKEN_1_TINT);

    SetHighColor(fill);
    FillRoundRect(bounds, fCornerRadius, fCornerRadius);

    SetHighColor(border);
    StrokeRoundRect(bounds, fCornerRadius, fCornerRadius);

    const char* label = Label();
    if (label && label[0] != '\0') {
        if (!IsEnabled())
            text = tint_color(text, B_DISABLED_LABEL_TINT);

        SetHighColor(text);
        SetDrawingMode(B_OP_OVER);

        font_height fh;
        GetFontHeight(&fh);
        float textW = StringWidth(label);
        BPoint pt(bounds.left + (bounds.Width() - textW) / 2.0f,
                  bounds.top + (bounds.Height() + fh.ascent - fh.descent) / 2.0f);
        DrawString(label, pt);

        SetDrawingMode(B_OP_COPY);
    }
}

void RoundButton::MouseDown(BPoint where)
{
    if (!IsEnabled())
        return;
    fTracking = true;
    SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
    Invalidate();
}

void RoundButton::MouseUp(BPoint where)
{
    if (fTracking) {
        fTracking = false;
        bool inside = Bounds().Contains(where);
        Invalidate();
        if (inside && IsEnabled())
            Invoke();
    }
    BControl::MouseUp(where);
}

void RoundButton::MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage)
{
    bool wasHovering = fHovering;
    fHovering = (code == B_ENTERED_VIEW || code == B_INSIDE_VIEW);
    if (fHovering != wasHovering)
        Invalidate();
    BControl::MouseMoved(where, code, dragMessage);
}

void RoundButton::SetEnabled(bool enabled)
{
    BControl::SetEnabled(enabled);
    Invalidate();
}
