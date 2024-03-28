// Copyright (C) 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#ifndef XRVIEWPORTCONTROLDIALOG_H
#define XRVIEWPORTCONTROLDIALOG_H

#include <QDialog>

namespace Ui {
class XrViewportControlDialog;
}

class XrViewportControlDialog : public QDialog {
    Q_OBJECT

public:
    explicit XrViewportControlDialog(QWidget* parent = nullptr);
    ~XrViewportControlDialog();

signals:
    void onXrViewportControlRequested(int control);

private slots:
    void on_btn_xr_viewport_pan_clicked();
    void on_btn_xr_viewport_zoom_clicked();
    void on_btn_xr_viewport_rotate_clicked();

private:
    Ui::XrViewportControlDialog* ui;
    bool mShown = false;
};

#endif  // XRVIEWPORTCONTROLDIALOG_H
