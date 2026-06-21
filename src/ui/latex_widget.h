#pragma once

#include <QString>

class QWidget;

namespace lg {

QWidget* createLatexDisplayWidget(const QString& latex, QWidget* parent);

}  // namespace lg
