#pragma once

#include <QString>
#include <QFlags>
#include <functional>

namespace acutesim::gui::widgets {

enum class DialogType { Message, Confirmation, TextInput, ItemSelection, CustomContent };
enum class DialogLevel { Info, Success, Warning, Error, Critical, Question };

enum class DialogButton : int {
    Ok      = 0x01,
    Cancel  = 0x02,
    Save    = 0x04,
    Discard = 0x08,
    Yes     = 0x10,
    No      = 0x20
};

Q_DECLARE_FLAGS(DialogButtons, DialogButton)
Q_DECLARE_OPERATORS_FOR_FLAGS(DialogButtons)

struct DialogResult {
    DialogButton clickedButton = DialogButton::Cancel;
    QString textValue;
    int selectedIndex = -1;
};

using DialogCallback = std::function<void(const DialogResult&)>;

} // namespace acutesim::gui::widgets
