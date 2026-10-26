// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

class QWidget;
class MainCtx;

// Keep the active control visible in a dialog's scrollable forms.
void trackScrollFocus(QWidget *root, MainCtx *ctx);
