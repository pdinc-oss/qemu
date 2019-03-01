/* Copyright (C) 2019 The Android Open Source Project
 **
 ** This software is licensed under the terms of the GNU General Public
 ** License version 2, as published by the Free Software Foundation, and
 ** may be copied, distributed, and modified under those terms.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 */

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#endif

#include <QtWidgets>

#include "android/emulator-window.h"
#include "android/featurecontrol/FeatureControl.h"
#include "android/hw-events.h"
#include "android/globals.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/stylesheet.h"
#include "android/skin/qt/tool-window-2.h"

#define  DEBUG  1

#if DEBUG
#include "android/utils/debug.h"

#define  D(...)   VERBOSE_PRINT(foldable,__VA_ARGS__)
#else
#define  D(...)   ((void)0)
#endif


namespace {

void ChangeIcon(QPushButton* button, const char* icon, const char* tip) {
    button->setIcon(getIconForCurrentTheme(icon));
    button->setProperty("themeIconName", icon);
    button->setProperty("toolTip", tip);
}
}  // namespace

using android::base::System;
using android::metrics::MetricsReporter;

namespace pb = android_studio;
namespace fc = android::featurecontrol;
using fc::Feature;

template <typename T>
ToolWindow2::WindowHolder2<T>::WindowHolder2(ToolWindow2* tw,
                                             OnCreatedCallback onCreated)
    : mWindow(new T(tw->mEmulatorWindow, tw))
{
    (tw->*onCreated)(mWindow);
}

template <typename T>
ToolWindow2::WindowHolder2<T>::~WindowHolder2() {
    // The window may have slots with subscribers, so use deleteLater() instead
    // of a regular delete for it.
    mWindow->deleteLater();
}

//const UiEmuAgent* ToolWindow2::sUiEmuAgent = nullptr;
static ToolWindow2* sToolWindow2 = nullptr;

ToolWindow2::ToolWindow2(EmulatorQtWindow* window,
                         QWidget* parent,
                         ToolWindow2::UIEventRecorderPtr event_recorder,
                         ToolWindow2::UserActionsCounterPtr user_actions_counter)
    : QFrame(parent),
      mEmulatorWindow(window),
      mTools2Ui(new Ui::ToolControls2),
      mUIEventRecorder(event_recorder),
      mUserActionsCounter(user_actions_counter) {
// "Tool" type windows live in another layer on top of everything in OSX, which
// is undesirable because it means the extended window must be on top of the
// emulator window. However, on Windows and Linux, "Tool" type windows are the
// only way to make a window that does not have its own taskbar item.
#ifdef __APPLE__
    Qt::WindowFlags flag = Qt::Dialog;
#else
    Qt::WindowFlags flag = Qt::Tool;
#endif
    setWindowFlags(flag | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);

    mTools2Ui->setupUi(this);

    // Get the latest user selections from the user-config code.
    SettingsTheme theme = getSelectedTheme();
    adjustAllButtonsForTheme(theme);
    updateTheme(Ui::stylesheetForTheme(theme));

    if (android_hw->hw_arc) {
        // Chrome OS doesn't support rotation now.
        mTools2Ui->rotateLeft ->setVisible(false);
        mTools2Ui->rotateRight->setVisible(false);
    }
    sToolWindow2 = this;

    //TODO: disable rotation for V1 release
    mTools2Ui->rotateLeft ->setVisible(false);
    mTools2Ui->rotateRight->setVisible(false);

    // Always assume unfolded starting status
    mTools2Ui->compressHoriz->setVisible(true);
    mTools2Ui->expandHoriz  ->setVisible(false);
}

ToolWindow2::~ToolWindow2() {
}

void ToolWindow2::closeEvent(QCloseEvent* ce) {
    mIsExiting = true;
    // make sure only parent processes the event - otherwise some
    // siblings won't get it, e.g. main window
    ce->ignore();
    setEnabled(false);
}

void ToolWindow2::mousePressEvent(QMouseEvent* event) {
    QFrame::mousePressEvent(event);
}

void ToolWindow2::show() {
    if (!shouldHide()) {
        QFrame::show();
    }
}

void ToolWindow2::forceShow() {
    QFrame::show();
}

void ToolWindow2::forceHide() {
    QFrame::hide();
}

void ToolWindow2::handleUICommand(QtUICommand cmd, bool down) {
    // TODO: Remove this?
}

//static
void ToolWindow2::forwardGenericEventToEmulator(int type, int code, int value) {
    EmulatorQtWindow* emuQtWindow = EmulatorQtWindow::getInstance();
    if (emuQtWindow == nullptr) {
        D("Error send Event, null emulator qt window\n");
        return;
    }

    SkinEvent* skin_event = new SkinEvent();
    skin_event->type = kEventGeneric;
    SkinEventGenericData& genericData = skin_event->u.generic_event;
    genericData.type = type;
    genericData.code = code;
    genericData.value = value;

    emuQtWindow->queueSkinEvent(skin_event);
}

bool ToolWindow2::handleQtKeyEvent(QKeyEvent* event, QtKeyEventSource source) {
    // We don't care about the keypad modifier for anything, and it gets added
    // to the arrow keys of OSX by default, so remove it.
    QKeySequence event_key_sequence(event->key() +
                                    (event->modifiers() & ~Qt::KeypadModifier));
    bool down = event->type() == QEvent::KeyPress;
    bool h = mShortcutKeyStore.handle(event_key_sequence,
                                      [this, down](QtUICommand cmd) {
                                          if (down) {
                                              handleUICommand(cmd, true);
                                              handleUICommand(cmd, false);
                                          }
                                      });
    return h;
}

void ToolWindow2::reportMouseButtonDown() {
    // TODO: Remove this?
}

void ToolWindow2::dockMainWindow() {
    // Align horizontally relative to the main window's frame.
    // Align vertically to its contents.
    // If we're frameless, adjust for a transparent border
    // around the skin.
    bool hasFrame;
    mEmulatorWindow->windowHasFrame(&hasFrame);
    int toolGap = hasFrame ? TOOL_GAP_FRAMED : TOOL_GAP_FRAMELESS;

    move(parentWidget()->frameGeometry().left()
             - frameGeometry().width()
             - toolGap + mEmulatorWindow->getLeftTransparency(),
         parentWidget()->geometry().top()
             + mEmulatorWindow->getTopTransparency());

    // Set the height to match the main window
    QSize size = this->size();
    size.setHeight(parentWidget()->geometry().height()
                    - mEmulatorWindow->getTopTransparency()
                    - mEmulatorWindow->getBottomTransparency() + 1);
    resize(size);
}

void ToolWindow2::updateTheme(const QString& styleSheet) {
    setStyleSheet(styleSheet);
}

void ToolWindow2::ensureExtendedWindowExists() {
    if (mToolWindow) {
        mToolWindow->ensureExtendedWindowExists();
    }
}

// static
void ToolWindow2::onMainLoopStart() {
}

void ToolWindow2::on_expandHoriz_clicked() {
    mTools2Ui->compressHoriz->setVisible(true);
    mTools2Ui->expandHoriz  ->setVisible(false);

    if (android_hw->hw_fold_adjust) {
        mEmulatorWindow->resizeAndChangeAspectRatio(false);
    }

    QSettings settings;
    settings.setValue(Ui::Settings::FOLDABLE_IS_FOLDED, false);

    D("sending SW_LID false\n");
    forwardGenericEventToEmulator(EV_SW, SW_LID, false);
    forwardGenericEventToEmulator(EV_SYN, 0, 0);
}

void ToolWindow2::on_compressHoriz_clicked() {
    mTools2Ui->compressHoriz->setVisible(false);
    mTools2Ui->expandHoriz  ->setVisible(true);

    if (android_hw->hw_fold_adjust) {
        mEmulatorWindow->resizeAndChangeAspectRatio(true);
    }

    QSettings settings;
    settings.setValue(Ui::Settings::FOLDABLE_IS_FOLDED, true);

    sendFoldedArea();

    D("sending SW_LID true\n");
    forwardGenericEventToEmulator(EV_SW, SW_LID, true);
    forwardGenericEventToEmulator(EV_SYN, 0, 0);
}

void ToolWindow2::on_rotateLeft_clicked() {
    ensureExtendedWindowExists();
    emulator_window_rotate_90(false); // False = left
}

void ToolWindow2::on_rotateRight_clicked() {
    ensureExtendedWindowExists();
    emulator_window_rotate_90(true); // True = right
}

void ToolWindow2::paintEvent(QPaintEvent*) {
    QPainter p;
    QPen pen(Qt::SolidLine);
    pen.setColor(Qt::black);
    pen.setWidth(1);
    p.begin(this);
    p.setPen(pen);

    double dpr = 1.0;
    int primary_screen_idx = qApp->desktop()->screenNumber(this);
    if (primary_screen_idx < 0) {
        primary_screen_idx = qApp->desktop()->primaryScreen();
    }
    const auto screens = QApplication::screens();
    if (primary_screen_idx >= 0 && primary_screen_idx < screens.size()) {
        const QScreen* const primary_screen = screens.at(primary_screen_idx);
        if (primary_screen) {
            dpr = primary_screen->devicePixelRatio();
        }
    }

    if (dpr > 1.0) {
        // Normally you'd draw the border with a (0, 0, w-1, h-1) rectangle.
        // However, there's some weirdness going on with high-density displays
        // that makes a single-pixel "slack" appear at the left and bottom
        // of the border. This basically adds 1 to compensate for it.
        p.drawRect(contentsRect());
    } else {
        p.drawRect(QRect(0, 0, width() - 1, height() - 1));
    }
    p.end();
}

//static
void ToolWindow2::earlyInitialization() {
    sendFoldedArea();
    // Force start with lid open state
    forwardGenericEventToEmulator(EV_SW, SW_LID, false);
    forwardGenericEventToEmulator(EV_SYN, 0, 0);
}

//static
void ToolWindow2::sendFoldedArea() {
    if (shouldHide()) {
        return;
    }

    EmulatorQtWindow* emuQtWindow = EmulatorQtWindow::getInstance();
    if (emuQtWindow == nullptr) return;

    int xOffset = android_hw->hw_displayRegion_0_1_xOffset;
    int yOffset = android_hw->hw_displayRegion_0_1_yOffset;
    int width   = android_hw->hw_displayRegion_0_1_width;
    int height  = android_hw->hw_displayRegion_0_1_height;
    char foldedArea[64];
    sprintf(foldedArea, "folded-area %d,%d,%d,%d",
            xOffset,
            yOffset,
            xOffset + width,
            yOffset + height);
    emuQtWindow->getAdbInterface()->enqueueCommand(
            {"shell", "wm", foldedArea},
            [](const android::emulation::OptionalAdbCommandResult& result) {
                if (result && result->exit_code == 0) {
                    D("foldable-page: 'fold-area' command succeeded\n");
                }});
}

//static
bool ToolWindow2::shouldHide() {
    int xOffset = android_hw->hw_displayRegion_0_1_xOffset;
    int yOffset = android_hw->hw_displayRegion_0_1_yOffset;
    int width   = android_hw->hw_displayRegion_0_1_width;
    int height  = android_hw->hw_displayRegion_0_1_height;
    int foldable  = android_hw->hw_foldable;
    QSettings settings;
    bool foldableEnabled = settings.value(Ui::Settings::FOLDABLE_ENABLE, false).toBool();

    if (xOffset < 0 || xOffset > 9999 ||
        yOffset < 0 || yOffset > 9999 ||
        width   < 1 || width   > 9999 ||
        height  < 1 || height  > 9999 ||
        !foldableEnabled              ||
        !foldable                     ||
        //TODO: need 29
        avdInfo_getApiLevel(android_avdInfo) < 28) {
        return true;
    }
    return false;
}
