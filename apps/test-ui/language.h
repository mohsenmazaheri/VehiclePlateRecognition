#pragma once
#include <QList>
#include <QString>
#include <QTranslator>

// Chooses the language of the whole application: its own texts, Qt's dialogs, the writing direction
// and the number format. A new language needs a translation file and one line in available().
class Language final {
public:
    struct Option {
        QString code;        // "en", "fa"
        QString nativeName;  // shown in the language list, never translated
    };

    [[nodiscard]] static QList<Option> available();
    // The language saved last time, otherwise the system language when available, otherwise English.
    [[nodiscard]] static QString preferred();

    // Installs the translations, sets the direction and number format, and remembers the choice.
    // Widgets receive QEvent::LanguageChange and must set their texts again.
    void apply(const QString& code);
    [[nodiscard]] const QString& current() const { return m_current; }

private:
    QTranslator m_application;
    QTranslator m_qt;
    QString m_current;
};
