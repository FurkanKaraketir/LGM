#include "mainwindow.h"

#include "app_settings.h"
#include "canvas.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>

#include "app_log.h"

static int runAnalyzeCli(const QString& path, const QStringList& outputs) {
    GraphScene scene;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QTextStream(stderr) << "Could not read: " << path << '\n';
        return 2;
    }
    QString error;
    if (!scene.documentFromJson(file.readAll(), &error)) {
        QTextStream(stderr) << error << '\n';
        return 2;
    }
    if (!outputs.isEmpty()) {
        scene.setOutputVariables(outputs);
    }
    const lg::NormalTreeEnumerationResult trees = scene.findAllNormalTrees();
    if (trees.trees.empty()) {
        QTextStream(stderr) << "No normal tree: " << trees.message << '\n';
        return 1;
    }
    QTextStream out(stdout);
    out << "normal_tree_order=" << trees.trees.front().stateVariables.size() << '\n';
    for (const lg::NormalTreeResult::StateVariable& state : trees.trees.front().stateVariables) {
        out << "state=" << state.symbol << '\n';
    }
    const lg::StateSpaceResult result = scene.computeStateSpaceRep();
    out << "status=" << static_cast<int>(result.status) << '\n';
    if (!result.message.isEmpty()) {
        out << "message=" << result.message << '\n';
    }
    if (result.status != lg::StateSpaceResult::Status::Ok) {
        return 1;
    }
    for (const QString& eq : result.stateEquations) {
        out << "state_eq=" << eq << '\n';
    }
    for (const QString& eq : result.outputEquations) {
        out << "output_eq=" << eq << '\n';
    }
    if (!result.outputMatrixForm.isEmpty()) {
        out << "output_matrix=" << result.outputMatrixForm << '\n';
    }
    return 0;
}

static QString openPathFromArgs(const QStringList& args) {
    for (const QString& arg : args) {
        if (arg.startsWith(QLatin1Char('-'))) {
            continue;
        }
        const QFileInfo info(arg);
        if (info.isFile() && info.suffix().compare(QStringLiteral("lgm"), Qt::CaseInsensitive) == 0) {
            return info.absoluteFilePath();
        }
    }
    return {};
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LGM"));
    app.setApplicationVersion(QStringLiteral(LGM_VERSION));
    app.setOrganizationName("LGM");
    app.setWindowIcon(QIcon(":/app_logo.ico"));

    applyAppTheme(AppSettings::load().theme);

    AppLog::install();

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--analyze")) {
        QStringList outputs;
        for (int i = 3; i < argc; ++i) {
            outputs.push_back(QString::fromLocal8Bit(argv[i]));
        }
        return runAnalyzeCli(QString::fromLocal8Bit(argv[2]), outputs);
    }

    MainWindow window;
    window.showMaximized();

    const QString openPath = openPathFromArgs(app.arguments().mid(1));
    if (!openPath.isEmpty()) {
        window.openDocument(openPath);
    }

    return app.exec();
}
