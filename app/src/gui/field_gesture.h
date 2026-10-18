// Number fields that edit the labels as one undo step per drag or typed entry, as a mouse drag in
// the bass roll is (LabelEditor gestures). A drag field reports a change on every frame of a drag,
// where the old -/+ fields changed once per click.
#pragma once

#include "io/label_editor.h"

#include <imgui.h>

namespace psyight {

class FieldGesture {
  public:
    // Once a frame, before the fields: closes the gesture once no field is active.
    void update(LabelEditor &labels) {
        if (open_ && !ImGui::IsAnyItemActive()) {
            labels.end_gesture();
            open_ = false;
        }
    }
    // Right after the field that changed, before the edit is applied.
    void before_edit(LabelEditor &labels) {
        if (!open_ && ImGui::IsItemActive()) {
            labels.begin_gesture();
            open_ = true;
        }
    }

  private:
    bool open_ = false;
};

} // namespace psyight
