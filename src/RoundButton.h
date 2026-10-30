#pragma once
#include <Control.h>

// A push button drawn by hand as a rounded rectangle (no system bevel),
// since BButton::SetFlat() still renders a square hover/pressed highlight.
class RoundButton : public BControl {
public:
    RoundButton(BRect frame, const char* name, const char* label, BMessage* message,
                uint32 resizingMode = B_FOLLOW_LEFT_TOP,
                uint32 flags = B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE);
    ~RoundButton() override;

    void Draw(BRect updateRect) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;
    void MouseMoved(BPoint where, uint32 code, const BMessage* dragMessage) override;
    void SetEnabled(bool enabled) override;

    // Default is 8.0 (pill-ish rounded rect); pass half the button's height
    // for a circular button (e.g. a small round "clear" button).
    void SetCornerRadius(float radius) { fCornerRadius = radius; }

private:
    bool  fHovering;
    bool  fTracking;
    float fCornerRadius;
};
