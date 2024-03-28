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
#include "xr-viewport-control-dialog.h"
#include "ui_xr-viewport-control-dialog.h"

#include "android/hw-sensors.h"

XrViewportControlDialog::XrViewportControlDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::XrViewportControlDialog) {
    ui->setupUi(this);
    setWindowFlags(Qt::Popup);
}

void XrViewportControlDialog::on_btn_xr_viewport_pan_clicked() {
    emit onXrViewportControlRequested(VIEWPORT_CONTROL_MODE_PAN);
    accept();  // hides dialog
}

void XrViewportControlDialog::on_btn_xr_viewport_zoom_clicked() {
    emit onXrViewportControlRequested(VIEWPORT_CONTROL_MODE_ZOOM);
    accept();  // hides dialog
}

void XrViewportControlDialog::on_btn_xr_viewport_rotate_clicked() {
    emit onXrViewportControlRequested(VIEWPORT_CONTROL_MODE_ROTATE);
    accept();  // hides dialog
}

XrViewportControlDialog::~XrViewportControlDialog() {
    delete ui;
}
