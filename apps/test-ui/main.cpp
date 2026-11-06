#include "language.h"
#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QPixmap>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("VehiclePlateRecognition"));

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption video(QStringLiteral("video"), QStringLiteral("Start with this video file."),
                                   QStringLiteral("file"));
    const QCommandLineOption snapshot(QStringLiteral("snapshot"),
                                      QStringLiteral("Run, save a window picture after --seconds, then exit. "
                                                     "Without --video this runs the synthetic demo."),
                                      QStringLiteral("file"));
    const QCommandLineOption seconds(QStringLiteral("seconds"), QStringLiteral("Delay of --snapshot."),
                                     QStringLiteral("number"), QStringLiteral("9"));
    const QCommandLineOption dataDirectory(QStringLiteral("data-dir"),
                                           QStringLiteral("Keep passages and photos in this folder."),
                                           QStringLiteral("folder"));
    const QCommandLineOption line(QStringLiteral("line"),
                                  QStringLiteral("Counting line as ax,ay,bx,by, each from 0 to 1 of the picture."),
                                  QStringLiteral("points"));
    const QCommandLineOption quitAtEnd(QStringLiteral("quit-at-end"),
                                       QStringLiteral("Exit when the video ends, after saving --snapshot if given."));
    const QCommandLineOption language(QStringLiteral("language"),
                                      QStringLiteral("Language of the window: en or fa. The choice is remembered."),
                                      QStringLiteral("code"));
    parser.addOptions({video, snapshot, seconds, dataDirectory, line, quitAtEnd, language});
    parser.process(app);

    Language languages;
    languages.apply(parser.isSet(language) ? parser.value(language) : Language::preferred());
    MainWindow window(languages, parser.value(dataDirectory));
    window.show();

    if (parser.isSet(line)) {
        const QStringList parts = parser.value(line).split(QLatin1Char(','));
        if (parts.size() != 4)
            parser.showHelp(1);
        window.setLine(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble());
    }

    if (parser.isSet(video))
        window.startVideo(parser.value(video));
    else if (parser.isSet(snapshot))
        window.start();

    if (parser.isSet(quitAtEnd)) {
        QObject::connect(&window, &MainWindow::sourceFinished, &window, [&window, path = parser.value(snapshot)]() {
            if (!path.isEmpty())
                window.grab().save(path);
            QApplication::quit();
        });
    } else if (parser.isSet(snapshot)) {
        QTimer::singleShot(parser.value(seconds).toInt() * 1000, &window, [&window, path = parser.value(snapshot)]() {
            window.grab().save(path);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
