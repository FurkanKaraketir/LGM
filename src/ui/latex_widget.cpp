#include "latex_widget.h"

#include "jkqtmathtext/jkqtmathtextlabel.h"

#include <QEvent>
#include <QPalette>

namespace {

class ThemedLatexLabel : public JKQTMathTextLabel {
public:
    explicit ThemedLatexLabel(QWidget* parent) : JKQTMathTextLabel(parent) {
        setAutoFillBackground(false);
        syncTheme();
    }

    void setLatex(const QString& latex) {
        m_latex = latex;
        renderLatex();
    }

protected:
    void changeEvent(QEvent* event) override {
        JKQTMathTextLabel::changeEvent(event);
        switch (event->type()) {
        case QEvent::PaletteChange:
        case QEvent::ApplicationPaletteChange:
        case QEvent::StyleChange:
            renderLatex();
            break;
        default:
            break;
        }
    }

private:
    QString m_latex;

    void syncTheme() {
        if (QWidget* host = parentWidget()) {
            setPalette(host->palette());
        }
        getMathText()->setFontColor(palette().color(QPalette::Text));
    }

    void renderLatex() {
        syncTheme();
        setMath(m_latex, true);
    }
};

}  // namespace

namespace lg {

QWidget* createLatexDisplayWidget(const QString& latex, QWidget* parent) {
    auto* label = new ThemedLatexLabel(parent);
    JKQTMathText* math = label->getMathText();
#ifdef JKQTMATHTEXT_COMPILED_WITH_XITS
    math->useXITS();
#endif
    math->setFontSize(11);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setWordWrap(false);
    label->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    label->setLatex(latex);
    return label;
}

}  // namespace lg
