#pragma once

#include <QWidget>

class GuideWindow : public QWidget {
    Q_OBJECT

public:
    static void showGuide(const QString& title, const QString& resourcePath, QWidget* parent = nullptr);

private:
    GuideWindow(const QString& title, const QString& resourcePath, QWidget* parent);
};
