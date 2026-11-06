#include "language.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace {

const QString kEnglish = QStringLiteral("en");

QSettings settings()
{
    return QSettings(QStringLiteral("VehiclePlateRecognition"), QStringLiteral("test-ui"));
}

bool isAvailable(const QString& code)
{
    for (const Language::Option& option : Language::available()) {
        if (option.code == code)
            return true;
    }
    return false;
}

} // namespace

QList<Language::Option> Language::available()
{
    return {{kEnglish, QStringLiteral("English")}, {QStringLiteral("fa"), QStringLiteral("فارسی")},
            {QStringLiteral("hy"), QStringLiteral("Հայերեն")}};
}

QString Language::preferred()
{
    const QString saved = settings().value(QStringLiteral("language")).toString();
    if (isAvailable(saved))
        return saved;
    const QString system = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    return isAvailable(system) ? system : kEnglish;
}

void Language::apply(const QString& requested)
{
    const QString code = isAvailable(requested) ? requested : kEnglish;
    QApplication::removeTranslator(&m_application);
    QApplication::removeTranslator(&m_qt);
    if (code != kEnglish) {
        // The texts are written in English, so English needs no translation file.
        if (m_application.load(QStringLiteral(":/i18n/vpr-test-ui_%1.qm").arg(code)))
            QApplication::installTranslator(&m_application);
        if (m_qt.load(QStringLiteral("qtbase_%1").arg(code), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QApplication::installTranslator(&m_qt);
    }
    const QLocale locale(code);
    QLocale::setDefault(locale);
    QApplication::setLayoutDirection(locale.textDirection());
    settings().setValue(QStringLiteral("language"), code);
    m_current = code;
}
